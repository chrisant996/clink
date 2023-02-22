// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "yield.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/globber.h>
#include <core/debugheap.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <process.h>
#include <share.h>
#include <list>
#include <memory>
#include <assert.h>

//------------------------------------------------------------------------------
struct popenrw_info;
static popenrw_info* s_head = nullptr;

//------------------------------------------------------------------------------
struct popenrw_info
{
    friend int io_popenyield(lua_State* state);
    friend int io_popenrw(lua_State* state);

    static popenrw_info* find(FILE* f)
    {
        for (popenrw_info* p = s_head; p; p = p->next)
        {
            if (f == p->r || f == p->w)
                return p;
        }
        return nullptr;
    }

    static void add(popenrw_info* p)
    {
        assert(p);
        p->next = s_head;
        s_head = p;
    }

    static bool remove(popenrw_info* p)
    {
        popenrw_info** pnext = &s_head;
        while (*pnext)
        {
            if (*pnext == p)
            {
                *pnext = p->next;
                p->next = nullptr;
                return true;
            }
            pnext = &(*pnext)->next;
        }
        return false;
    }

    popenrw_info()
    : next(nullptr)
    , r(nullptr)
    , w(nullptr)
    , process_handle(0)
    , async(false)
    {
    }

    ~popenrw_info()
    {
        assert(!next);
        assert(!r);
        assert(!w);
        assert(!process_handle);
    }

    int close(FILE* f)
    {
        int ret = -1;
        if (f)
        {
            assert(f == r || f == w);
            ret = fclose(f);
            if (f == r) r = nullptr;
            if (f == w) w = nullptr;
        }
        return ret;
    }

    intptr_t get_wait_handle()
    {
        if (r || w)
            return 0;
        intptr_t wait = process_handle;
        process_handle = 0;
        return wait;
    }

    bool is_async()
    {
        return async;
    }

private:
    popenrw_info* next;
    FILE* r;
    FILE* w;
    intptr_t process_handle;
    bool async;
};

//------------------------------------------------------------------------------
static int pclosewait(intptr_t process_handle)
{
    int return_value = -1;

    errno_t const saved_errno = errno;
    errno = 0;

    int status = 0;
    if (_cwait(&status, process_handle, _WAIT_GRANDCHILD) != -1 || errno == EINTR)
        return_value = status;

    errno = saved_errno;

    return return_value;
}

//------------------------------------------------------------------------------
static int pclosefile(lua_State *state)
{
    luaL_Stream* p = ((luaL_Stream*)luaL_checkudata(state, 1, LUA_FILEHANDLE));
    assert(p);
    if (!p)
        return 0;

    popenrw_info* info = popenrw_info::find(p->f);
    assert(info);
    if (!info)
        return luaL_fileresult(state, false, NULL);

    int res = info->close(p->f);
    intptr_t process_handle = info->get_wait_handle();
    if (process_handle)
    {
        int stat = 0;
        bool wait = !info->is_async();
        popenrw_info::remove(info);
        delete info;
        if (wait)
            stat = pclosewait(process_handle);
        else
            CloseHandle(HANDLE(process_handle));
        return luaL_execresult(state, stat);
    }

    return luaL_fileresult(state, (res == 0), NULL);
}

//------------------------------------------------------------------------------
struct pipe_pair
{
    ~pipe_pair()
    {
        if (remote) CloseHandle(remote);
        if (local)  fclose(local);
    }

    bool init(bool write, bool binary)
    {
        int handles[2] = { -1, -1 };
        int index_local = write ? 1 : 0;
        int index_remote = 1 - index_local;

        int pipe_mode = _O_NOINHERIT | (binary ? _O_BINARY : _O_TEXT);
        if (_pipe(handles, 1024, pipe_mode) != -1)
        {
            static const wchar_t* const c_mode[2][2] =
            {
                { L"rt", L"rb" },   // !write
                { L"wt", L"wb" },   // write
            };

            local = _wfdopen(handles[index_local], c_mode[write][binary]);
            if (local)
                remote = os::dup_handle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(handles[index_remote])), true/*inherit*/);

            errno_t e = errno;
            _close(handles[index_remote]);
            if (!local)
                _close(handles[index_local]);
            errno = e;
        }

        if (local && remote)
            return true;

        errno_t e = errno;
        if (local) fclose(local);
        if (remote) CloseHandle(remote);
        local = nullptr;
        remote = 0;
        errno = e;

        return false;
    }

    void transfer_local()
    {
        local = nullptr;
    }

    HANDLE remote = 0;
    FILE* local = nullptr;
};



//------------------------------------------------------------------------------
struct popen_buffering : public yield_thread
{
    popen_buffering(FILE* r, HANDLE w)
    : m_read(r)
    , m_write(w)
    {
        assert(r != nullptr);
        assert(w != nullptr);
        assert(w != INVALID_HANDLE_VALUE);
    }

    ~popen_buffering()
    {
        if (m_read)
            fclose(m_read);
        if (m_write)
            CloseHandle(m_write);
        if (m_stat_event)
            CloseHandle(m_stat_event);
        if (m_process_handle)
            CloseHandle(m_process_handle);
    }

    bool createthread()
    {
        assert(!m_stat_event);
        m_stat_event = CreateEvent(nullptr, true, false, nullptr);
        if (!m_stat_event)
            return false;
        return yield_thread::createthread();
    }

    void go(HANDLE process_handle)
    {
        assert(!m_process_handle);
        m_process_handle = os::dup_handle(GetCurrentProcess(), process_handle);
        yield_thread::go();
    }

    HANDLE get_ready_event() override
    {
        if (m_need_completion)
            return m_stat_event;
        return yield_thread::get_ready_event();
    }

    void set_need_completion() override
    {
        m_need_completion = true;
    }

    int results(lua_State* state) override
    {
        errno = m_errno;
        return luaL_execresult(state, m_stat);
    }

private:
    void do_work() override
    {
        HANDLE rh = reinterpret_cast<HANDLE>(_get_osfhandle(fileno(m_read)));
        HANDLE wh = m_write;

        while (!is_canceled())
        {
            DWORD len;
            if (!ReadFile(rh, m_buffer, sizeof_array(m_buffer), &len, nullptr))
                break;

            DWORD written;
            if (!WriteFile(wh, m_buffer, len, &written, nullptr))
                break;
            if (written != len)
                break;
        }

        // Reset file pointer so the read handle can read from the beginning.
        SetFilePointer(wh, 0, nullptr, FILE_BEGIN);

        // Close the write handle since it's finished.
        CloseHandle(m_write);
        m_write = nullptr;
    }

    bool do_completion() override
    {
        if (!m_stat_event || !m_process_handle)
            return false;

        m_stat = pclosewait(intptr_t(m_process_handle));
        m_errno = errno;
        m_process_handle = 0;
        SetEvent(m_stat_event);
        return true;
    }

    FILE*           m_read;
    HANDLE          m_write;
    HANDLE          m_stat_event = 0;
    HANDLE          m_process_handle = 0;

    int             m_stat = -1;
    errno_t         m_errno = 0;
    volatile long   m_need_completion = false;

    BYTE            m_buffer[4096];
};



//------------------------------------------------------------------------------
/// -name:  io.popenrw
/// -ver:   1.1.42
/// -arg:   command:string
/// -arg:   [mode:string]
/// -ret:   file, file
/// Runs <span class="arg">command</span> and returns two file handles:  a file
/// handle for reading output from the command, and a file handle for writing
/// input to the command.
///
/// <span class="arg">mode</span> can be "t" for text mode (the default if
/// omitted) or "b" for binary mode.
///
/// If the function fails it returns nil, an error message, and an error number.
///
/// <fieldset><legend>Warning</legend>
/// This can result in deadlocks unless the command fully reads all of its input
/// before writing any output.  This is because Lua uses blocking IO to read and
/// write file handles.  If the write buffer fills (or the read buffer is empty)
/// then the write (or read) will block and can only become unblocked if the
/// command correspondingly reads (or writes).  But the other command can easily
/// experience the same blocking IO problem on its end, resulting in a deadlock:
/// process 1 is blocked from writing more until process 2 reads, but process 2
/// can't read because it is blocked from writing until process 1 reads.
/// </fieldset>
/// -show:  local r,w = io.popenrw("fzf.exe --height 40%")
/// -show:
/// -show:  w:write("hello\n")
/// -show:  w:write("world\n")
/// -show:  w:close()
/// -show:
/// -show:  while (true) do
/// -show:  &nbsp;   local line = r:read("*line")
/// -show:  &nbsp;   if not line then
/// -show:  &nbsp;       break
/// -show:  &nbsp;   end
/// -show:  &nbsp;   print(line)
/// -show:  end
/// -show:  r:close()
/*static*/ int io_popenrw(lua_State* state) // gcc can't handle 'friend' and 'static'.
{
    const char* command = checkstring(state, 1);
    const char* mode = optstring(state, 2, "t");
    if (!command || !mode)
        return 0;
    if ((mode[0] != 'b' && mode[0] != 't') || mode[1])
        return luaL_error(state, "invalid mode " LUA_QS
                          " (use " LUA_QL("t") ", " LUA_QL("b") ", or nil)", mode);

    luaL_Stream* pr = nullptr;
    luaL_Stream* pw = nullptr;
    pipe_pair pipe_stdin;
    pipe_pair pipe_stdout;

    pr = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
    luaL_setmetatable(state, LUA_FILEHANDLE);
    pr->f = nullptr;
    pr->closef = nullptr;

    pw = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
    luaL_setmetatable(state, LUA_FILEHANDLE);
    pw->f = nullptr;
    pw->closef = nullptr;

    bool binary = (mode[0] == 'b');
    bool failed = (!pipe_stdin.init(true/*write*/, binary) ||
                   !pipe_stdout.init(false/*write*/, binary));

    if (!failed)
    {
        popenrw_info* info = new popenrw_info;
        HANDLE process_handle = os::spawn_internal(command, nullptr, pipe_stdin.remote, pipe_stdout.remote);

        if (!process_handle)
        {
            errno_t e = errno;

            failed = true;
            lua_pop(state, 2);
            delete info;

            errno = e;
        }
        else
        {
            pr->f = pipe_stdout.local;
            pw->f = pipe_stdin.local;
            pr->closef = &pclosefile;
            pw->closef = &pclosefile;

            info->r = pr->f;
            info->w = pw->f;
            info->process_handle = reinterpret_cast<intptr_t>(process_handle);
            popenrw_info::add(info);

            pipe_stdin.transfer_local();
            pipe_stdout.transfer_local();
        }
    }

    return (failed) ? luaL_fileresult(state, 0, command) : 2;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.  See io.popenyield in coroutines.lua.
/*static*/ int io_popenyield(lua_State* state) // gcc can't handle 'friend' and 'static'.
{
    const char* command = checkstring(state, 1);
    const char* mode = optstring(state, 2, "t");
    if (!command || !mode)
        return 0;

    bool binary = false;
    if (*mode == 'r')
        mode++;
    if (*mode == 'b')
        binary = true, mode++;
    else if (*mode == 't')
        binary = false, mode++;
    if (*mode)
        return luaL_error(state, "invalid mode " LUA_QS
                          " (should match " LUA_QL("r?[bt]?") " or nil)", mode);

    luaL_Stream* pr = nullptr;
    luaL_YieldGuard* yg = nullptr;
    pipe_pair pipe_stdin;
    pipe_pair pipe_stdout;

    pr = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
    luaL_setmetatable(state, LUA_FILEHANDLE);
    pr->f = nullptr;
    pr->closef = nullptr;

    yg = luaL_YieldGuard::make_new(state);

    bool failed = true;
    FILE* temp_read = nullptr;
    HANDLE temp_write = nullptr;
    std::shared_ptr<popen_buffering> buffering;
    popenrw_info* info = nullptr;

    do
    {
        dbg_ignore_scope(snapshot, "Lua io_popenyield");

        os::temp_file_mode tfmode = os::temp_file_mode::delete_on_close;
        if (binary)
            tfmode |= os::temp_file_mode::binary;
        str<> name;
        temp_read = os::create_temp_file(&name, "clk", ".tmp", tfmode);
        if (!temp_read)
            break;

        temp_write = os::dup_handle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(fileno(temp_read))), true/*inherit*/);
        if (!temp_write)
            break;

        // The pipe and temp_write are both binary to simplify the thread's job.
        // Must provide pipe_stdin to the spawned process, or some processes may
        // error out due to missing stdin handle (e.g. FC and XCOPY).
        if (!pipe_stdin.init(true/*write*/, true/*binary*/) ||
            !pipe_stdout.init(false/*write*/, true/*binary*/))
            break;

        buffering = std::make_shared<popen_buffering>(pipe_stdout.local, temp_write);
        pipe_stdout.transfer_local();
        temp_write = nullptr;
        if (!buffering->createthread())
            break;

        info = new popenrw_info;
        HANDLE process_handle = os::spawn_internal(command, nullptr, pipe_stdin.remote, pipe_stdout.remote);
        if (!process_handle)
            break;

        pr->f = temp_read;
        pr->closef = &pclosefile;
        temp_read = nullptr;

        info->r = pr->f;
        info->process_handle = reinterpret_cast<intptr_t>(process_handle);
        info->async = true;
        popenrw_info::add(info);
        info = nullptr;

        yg->init(buffering, command);
        buffering->go(process_handle);

        failed = false;
    }
    while (false);

    // Preserve errno when releasing all the resources.
    {
        errno_t e = errno;

        if (temp_read)
            fclose(temp_read);
        if (temp_write)
            CloseHandle(temp_write);
        delete info;
        buffering = nullptr;

        if (failed)
            lua_pop(state, 2);

        errno = e;
    }

    return (failed) ? luaL_fileresult(state, 0, command) : 2;
}

//------------------------------------------------------------------------------
/// -name:  io.open
/// -ver:   0.0.1
/// -arg:   filename:string
/// -arg:   [mode:string]
/// -ret:   file
/// This function opens a file named by <span class="arg">filename</span>, in
/// the mode specified in the string <span class="arg">mode</span>.  It returns
/// a new file handle, or, in case of errors, nil plus an error message and
/// error number.
///
/// The <span class="arg">mode</span> string can be any of the following:
/// <ul>
/// <li><code>"r"</code>: read mode (the default);
/// <li><code>"w"</code>: write mode;
/// <li><code>"wx"</code>: write mode, but fail if the file already exists (requires v1.3.18 or higher);
/// <li><code>"a"</code>: append mode;
/// <li><code>"r+"</code>: update mode, all previous data is preserved;
/// <li><code>"w+"</code>: update mode, all previous data is erased;
/// <li><code>"w+x"</code>: update mode, all previous data is erased, but fail if the file already exists (requires v1.3.18 or higher);
/// <li><code>"a+"</code>: append update mode, previous data is preserved, writing is only allowed at the end of file.
/// </ul>
///
/// The <span class="arg">mode</span> string can also have a <code>'b'</code> at
/// the end to open the file in binary mode.
///
/// The <code>'x'</code> modes are Clink extensions to Lua.

//------------------------------------------------------------------------------
static int io_sclose(lua_State* state)
{
    luaL_Stream* p = ((luaL_Stream*)luaL_checkudata(state, 1, LUA_FILEHANDLE));
    int res = fclose(p->f);
    return luaL_fileresult(state, (res == 0), NULL);
}

//------------------------------------------------------------------------------
/// -name:  io.sopen
/// -ver:   1.3.18
/// -arg:   filename:string
/// -arg:   [mode:string]
/// -arg:   [deny:string]
/// -ret:   file
/// This is the same as <a href="#io.open">io.open()</a>, but adds an optional
/// <code>deny</code> argument that specifies the type of sharing allowed.
///
/// This function opens a file named by <span class="arg">filename</span>, in
/// the mode specified in the string <span class="arg">mode</span>.  It returns
/// a new file handle, or, in case of errors, nil plus an error message and
/// error number.
///
/// The <span class="arg">mode</span> string can be any of the following:
/// <ul>
/// <li><code>"r"</code>: read mode (the default);
/// <li><code>"w"</code>: write mode;
/// <li><code>"wx"</code>: write mode, but fail if the file already exists;
/// <li><code>"a"</code>: append mode;
/// <li><code>"r+"</code>: update mode, all previous data is preserved;
/// <li><code>"w+"</code>: update mode, all previous data is erased;
/// <li><code>"w+x"</code>: update mode, all previous data is erased, but fail if the file already exists;
/// <li><code>"a+"</code>: append update mode, previous data is preserved, writing is only allowed at the end of file.
/// </ul>
///
/// The <span class="arg">mode</span> string can also have a <code>'b'</code> at
/// the end to open the file in binary mode.
///
/// The <span class="arg">deny</span> string can be any of the following:
/// <ul>
/// <li><code>"r"</code> denies read access;
/// <li><code>"w"</code> denies write access;
/// <li><code>"rw"</code> denies read and write access;
/// <li><code>""</code> permits read and write access (the default).
/// </ul>
static int io_sopen(lua_State* state)
{
    const char* filename = luaL_checkstring(state, 1);
    const char* mode = luaL_optstring(state, 2, "r");
    const char* deny = luaL_optstring(state, 3, "");

    luaL_Stream* p = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
    luaL_setmetatable(state, LUA_FILEHANDLE);
    p->f = NULL;
    p->closef = &io_sclose;

    const char *md = mode; /* to traverse/check mode */
    luaL_argcheck(state, lua_checkmode(md), 2, "invalid mode");

    int share = 0;
    if (!deny[0])
    {
        share = _SH_DENYNO;
    }
    else if (deny[0] == 'r')
    {
        if (!deny[1])
            share = _SH_DENYRD;
        else if (deny[1] == 'w' && !deny[2])
            share = _SH_DENYRW;
    }
    else if (deny[0] == 'w')
    {
        if (!deny[1])
            share = _SH_DENYWR;
        else if (deny[1] == 'r' && !deny[2])
            share = _SH_DENYRW;
    }
    luaL_argcheck(state, (share != 0), 3, "invalid deny");

    p->f = _fsopen(filename, mode, share);
    return (p->f == NULL) ? luaL_fileresult(state, 0, filename) : 1;
}

//------------------------------------------------------------------------------
/// -name:  io.truncate
/// -ver:   1.3.41
/// -arg:   file:file
/// -ret:   boolean
/// This function truncates the <span class="arg">file</span> previously opened
/// by <a href="#io.open">io.open()</a> or <a href="#io.sopen">io.sopen()</a>.
/// When used on a pipe or other file handle that doesn't refer to an actual
/// file, the behavior is undefined.
///
/// If successful, the return value is true. If an error occurs, the return value 
/// is false, an error message, and an error code.
static int io_truncate(lua_State* state)
{
    luaL_Stream* file = ((luaL_Stream *)luaL_checkudata(state, 1, LUA_FILEHANDLE));
    assert(file);
    if (!file)
        return 0;

    int stat = fflush(file->f);
    if (stat)
    {
failed:
        return luaL_fileresult(state, false, NULL);
    }

    int fd = _fileno(file->f);
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));

    LARGE_INTEGER liSize;
    LARGE_INTEGER liZero = {};
    if (!SetFilePointerEx(h, liZero, &liSize, FILE_CURRENT))
        goto failed;

    if (_chsize_s(fd, liSize.QuadPart))
        goto failed;

    return luaL_fileresult(state, true, NULL);
}

//------------------------------------------------------------------------------
void io_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "popenrw",                    &io_popenrw },
        { "popenyield_internal",        &io_popenyield },
        { "sopen",                      &io_sopen },
        { "truncate",                   &io_truncate },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "io");

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pop(state, 1);
}
