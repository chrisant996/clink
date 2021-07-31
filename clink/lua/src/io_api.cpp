// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/globber.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <process.h>
#include <list>
#include <memory>
#include <assert.h>

#ifndef _MSC_VER
#define USE_PORTABLE
#endif

//------------------------------------------------------------------------------
struct popenrw_info;
static popenrw_info* s_head = nullptr;
static HANDLE s_wake_event = nullptr;

//------------------------------------------------------------------------------
void set_io_wake_event(HANDLE event)
{
    // Borrow a ref from the caller.
    s_wake_event = event;
}

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
        bool wait = !info->is_async();
        popenrw_info::remove(info);
        delete info;
        return luaL_execresult(state, wait ? pclosewait(process_handle) : 0);
    }

    return luaL_fileresult(state, (res == 0), NULL);
}

//------------------------------------------------------------------------------
static HANDLE dup_handle(HANDLE process_handle, HANDLE h)
{
    HANDLE new_h = 0;
    if (!DuplicateHandle(process_handle,
                         h,
                         process_handle,
                         &new_h,
                         0,
                         true/*bInheritHandle*/,
                         DUPLICATE_SAME_ACCESS))
    {
        os::map_errno();
        return 0;
    }
    return new_h;
}

//------------------------------------------------------------------------------
#ifndef USE_PORTABLE
extern "C" wchar_t* __cdecl __acrt_wgetpath(
    wchar_t const* const delimited_paths,
    wchar_t*       const result,
    size_t         const result_count
    );
#endif

//------------------------------------------------------------------------------
static bool search_path(wstr_base& out, const wchar_t* file)
{
#ifndef USE_PORTABLE

    wstr_moveable wpath;
    {
        int len = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (len)
        {
            wpath.reserve(len);
            len = GetEnvironmentVariableW(L"COMSPEC", wpath.data(), wpath.size());
        }
    }

    if (!wpath.length())
        return false;

    wchar_t buf[MAX_PATH];
    wstr_base buffer(buf);

    const wchar_t* current = wpath.c_str();
    while ((current = __acrt_wgetpath(current, buffer.data(), buffer.size() - 1)) != 0)
    {
        unsigned int len = buffer.length();
        if (len && !path::is_separator(buffer.c_str()[len - 1]))
        {
            if (!buffer.concat(L"\\"))
                continue;
        }

        if (!buffer.concat(file))
            continue;

        DWORD attr = GetFileAttributesW(buffer.c_str());
        if (attr != 0xffffffff)
        {
            out.clear();
            out.concat(buffer.c_str(), buffer.length());
            return true;
        }
    }

    return false;

#else

    wchar_t buf[MAX_PATH];

    wchar_t* file_part;
    DWORD dw = SearchPathW(nullptr, file, nullptr, sizeof_array(buf), buf, &file_part);
    if (dw == 0 || dw >= sizeof_array(buf))
        return false;

    out.clear();
    out.concat(buf, dw);
    return true;

#endif
}

//------------------------------------------------------------------------------
static intptr_t popenrw_internal(const char* command, HANDLE hStdin, HANDLE hStdout)
{
    // Determine which command processor to use:  command.com or cmd.exe:
    static wchar_t const default_cmd_exe[] = L"cmd.exe";
    wstr_moveable comspec;
    const wchar_t* cmd_exe = default_cmd_exe;
    {
        int len = GetEnvironmentVariableW(L"COMSPEC", nullptr, 0);
        if (len)
        {
            comspec.reserve(len);
            len = GetEnvironmentVariableW(L"COMSPEC", comspec.data(), comspec.size());
            if (len)
                cmd_exe = comspec.c_str();
        }
    }

    STARTUPINFOW startup_info = { 0 };
    startup_info.cb = sizeof(startup_info);

    // The following arguments are used by the OS for duplicating the handles:
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput  = hStdin;
    startup_info.hStdOutput = hStdout;
    startup_info.hStdError  = reinterpret_cast<HANDLE>(_get_osfhandle(2));

    wstr<> command_line;
    command_line << cmd_exe;
    command_line << L" /c ";
    to_utf16(command_line, command);

    // Find the path at which the executable is accessible:
    wstr_moveable selected_cmd_exe;
    DWORD attrCmd = GetFileAttributesW(cmd_exe);
    if (attrCmd == 0xffffffff)
    {
        errno_t e = errno;
        if (!search_path(selected_cmd_exe, cmd_exe))
        {
            errno = e;
            return 0;
        }
        cmd_exe = selected_cmd_exe.c_str();
    }

    PROCESS_INFORMATION process_info = PROCESS_INFORMATION();
    BOOL const child_status = CreateProcessW(
        cmd_exe,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE/*bInheritHandles*/,
        0,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);

    if (!child_status)
    {
        os::map_errno();
        return 0;
    }

    CloseHandle(process_info.hThread);
    return reinterpret_cast<intptr_t>(process_info.hProcess);
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
                remote = dup_handle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(handles[index_remote])));

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
struct popen_buffering : public std::enable_shared_from_this<popen_buffering>
{
    popen_buffering(FILE* r, HANDLE w)
    : m_read(r)
    , m_write(w)
    {
    }

    ~popen_buffering()
    {
        if (m_thread_handle)
        {
            cancel();
            CloseHandle(m_thread_handle);
        }
        if (m_read)
            fclose(m_read);
        if (m_write)
            CloseHandle(m_write);
        if (m_ready_event)
            CloseHandle(m_ready_event);
        if (m_wake_event)
            CloseHandle(m_wake_event);
    }

    bool createthread()
    {
        assert(!m_suspended);
        assert(!m_thread_handle);
        assert(!m_cancelled);
        assert(!m_ready_event);
        assert(!m_wake_event);
        if (s_wake_event)
        {
            m_wake_event = dup_handle(GetCurrentProcess(), s_wake_event);
            if (!m_wake_event)
                return false;
        }
        m_ready_event = CreateEvent(nullptr, true, false, nullptr);
        if (!m_ready_event)
            return false;
        m_thread_handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &threadproc, this, CREATE_SUSPENDED, nullptr));
        if (!m_thread_handle)
            return false;
        m_holder = shared_from_this(); // Now threadproc holds a strong ref.
        m_suspended = true;
        return true;
    }

    void go()
    {
        assert(m_suspended);
        if (m_thread_handle)
        {
            m_suspended = false;
            ResumeThread(m_thread_handle);
        }
    }

    void cancel()
    {
        m_cancelled = true;
        if (m_suspended) // Can only be true when there's no concurrency.
            ResumeThread(m_thread_handle);
    }

    bool is_ready()
    {
        if (!m_ready_event)
            return false;
        return WaitForSingleObject(m_ready_event, 0) == WAIT_OBJECT_0;
    }

    HANDLE get_ready_event()
    {
        return m_ready_event;
    }

private:
    static unsigned __stdcall threadproc(void* arg)
    {
        popen_buffering* _this = static_cast<popen_buffering*>(arg);
        HANDLE rh = reinterpret_cast<HANDLE>(_get_osfhandle(fileno(_this->m_read)));
        HANDLE wh = _this->m_write;

        while (!_this->m_cancelled)
        {
            DWORD len;
TODO("COROUTINES: could use overlapped IO to enable cancelling even a blocking call.");
            if (!ReadFile(rh, _this->m_buffer, sizeof_array(m_buffer), &len, nullptr))
                break;

            DWORD written;
            if (!WriteFile(wh, _this->m_buffer, len, &written, nullptr))
                break;
            if (written != len)
                break;
        }

        // Reset file pointer so the read handle can read from the beginning.
        SetFilePointer(wh, 0, nullptr, FILE_BEGIN);

        // Signal completion events.
        SetEvent(_this->m_ready_event);
        if (_this->m_wake_event)
            SetEvent(_this->m_wake_event);

        // Release threadproc's strong ref.
        _this->m_holder = nullptr;

        _endthreadex(0);
        return 0;
    }

    FILE* m_read;
    HANDLE m_write;
    HANDLE m_thread_handle = 0;
    HANDLE m_ready_event = 0;
    HANDLE m_wake_event = 0;
    bool m_suspended = false;

    volatile long m_cancelled = false;
    volatile long m_ready = false;

    std::shared_ptr<popen_buffering> m_holder;
    BYTE m_buffer[4096];
};



//------------------------------------------------------------------------------
#define LUA_YIELDGUARD "clink_yield_guard"
struct luaL_YieldGuard
{
    static luaL_YieldGuard* make_new(lua_State* state);

    void init(std::shared_ptr<popen_buffering>& buffering, const char* command);

private:
    static int ready(lua_State* state);
    static int command(lua_State* state);
    static int __gc(lua_State* state);
    static int __tostring(lua_State* state);

    std::shared_ptr<popen_buffering> m_buffering;
    str_moveable m_command;
};

//------------------------------------------------------------------------------
luaL_YieldGuard* luaL_YieldGuard::make_new(lua_State* state)
{
#ifdef DEBUG
    int oldtop = lua_gettop(state);
#endif

    luaL_YieldGuard* yg = (luaL_YieldGuard*)lua_newuserdata(state, sizeof(luaL_YieldGuard));
    new (yg) luaL_YieldGuard();

    static const luaL_Reg yglib[] =
    {
        {"ready", ready},
        {"command", luaL_YieldGuard::command}, // Ambiguous because of command arg.
        {"__gc", __gc},
        {"__tostring", __tostring},
        {nullptr, nullptr}
    };

    if (luaL_newmetatable(state, LUA_YIELDGUARD))
    {
        lua_pushvalue(state, -1);           // push metatable
        lua_setfield(state, -2, "__index"); // metatable.__index = metatable
        luaL_setfuncs(state, yglib, 0);     // add methods to new metatable
    }
    lua_setmetatable(state, -2);

#ifdef DEBUG
    int newtop = lua_gettop(state);
    assert(oldtop - newtop == -1);
    luaL_YieldGuard* test = (luaL_YieldGuard*)luaL_checkudata(state, -1, LUA_YIELDGUARD);
    assert(test == yg);
#endif

    return yg;
}

//------------------------------------------------------------------------------
void luaL_YieldGuard::init(std::shared_ptr<popen_buffering>& buffering, const char* command)
{
    m_buffering = buffering;
    m_command = command;
}

//------------------------------------------------------------------------------
int luaL_YieldGuard::ready(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    lua_pushboolean(state, yg->m_buffering->is_ready());
    return 1;
}

//------------------------------------------------------------------------------
int luaL_YieldGuard::command(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    lua_pushstring(state, yg->m_command.c_str());
    return 1;
}

//------------------------------------------------------------------------------
int luaL_YieldGuard::__gc(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    yg->~luaL_YieldGuard();
    return 0;
}

//------------------------------------------------------------------------------
int luaL_YieldGuard::__tostring(lua_State* state)
{
    luaL_YieldGuard* yg = (luaL_YieldGuard*)luaL_checkudata(state, 1, LUA_YIELDGUARD);
    lua_pushfstring(state, "yieldguard (%p)", yg->m_buffering.get());
    return 1;
}



//------------------------------------------------------------------------------
/// -name:  io.popenrw
/// -arg:   command:string
/// -arg:   [mode:string]
/// -ret:   file, file
/// -show:  local r,w = io.popenrw("fzf.exe --height 40%")
/// -show:
/// -show:  w:write("hello\n")
/// -show:  w:write("world\n")
/// -show:  w:close()
/// -show:
/// -show:  while (true) do
/// -show:  &nbsp; local line = r:read("*line")
/// -show:  &nbsp; if not line then
/// -show:  &nbsp;   break
/// -show:  &nbsp; end
/// -show:  &nbsp; print(line)
/// -show:  end
/// -show:  r:close()
/// Runs <code>command</code> and returns two file handles:  a file handle for
/// reading output from the command, and a file handle for writing input to the
/// command.
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
        intptr_t process_handle = popenrw_internal(command, pipe_stdin.remote, pipe_stdout.remote);

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
            info->process_handle = process_handle;
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
        os::temp_file_mode tfmode = os::temp_file_mode::delete_on_close;
        if (binary)
            tfmode |= os::temp_file_mode::binary;
        str<> name;
        temp_read = os::create_temp_file(&name, "clk", ".tmp", tfmode);
        if (!temp_read)
            break;

        temp_write = dup_handle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(fileno(temp_read))));
        if (!temp_write)
            break;

        // The pipe and temp_write are both binary to simplify the thread's job.
        if (!pipe_stdout.init(false/*write*/, true/*binary*/))
            break;

        buffering = std::make_shared<popen_buffering>(pipe_stdout.local, temp_write);
        pipe_stdout.transfer_local();
        temp_write = nullptr;
        if (!buffering->createthread())
            break;

        info = new popenrw_info;
        intptr_t process_handle = popenrw_internal(command, NULL, pipe_stdout.remote);
        if (!process_handle)
            break;

        pr->f = temp_read;
        pr->closef = &pclosefile;
        temp_read = nullptr;

        info->r = pr->f;
        info->process_handle = process_handle;
        info->async = true;
        popenrw_info::add(info);
        info = nullptr;

        yg->init(buffering, command);
        buffering->go();

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
void io_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "popenrw",                    &io_popenrw },
        { "popenyield_internal",        &io_popenyield },
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
