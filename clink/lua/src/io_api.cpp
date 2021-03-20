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
#include <process.h>
#include <list>
#include <assert.h>

//------------------------------------------------------------------------------
struct popenrw_info;
static popenrw_info* s_head = nullptr;

//------------------------------------------------------------------------------
struct popenrw_info
{
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

private:
    popenrw_info* next;
    FILE* r;
    FILE* w;
    intptr_t process_handle;
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
    popenrw_info* info = popenrw_info::find(p->f);

    assert(info);
    if (!info)
        return luaL_fileresult(state, false, NULL);

    int res = info->close(p->f);
    intptr_t process_handle = info->get_wait_handle();
    if (process_handle)
    {
        popenrw_info::remove(info);
        delete info;
        return luaL_execresult(state, pclosewait(process_handle));
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
        return 0;
    return new_h;
}

//------------------------------------------------------------------------------
extern "C" wchar_t* __cdecl __acrt_wgetpath(
    wchar_t const* const delimited_paths,
    wchar_t*       const result,
    size_t         const result_count
    );
static bool search_path(wstr_base& out, const wchar_t* file)
{
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
        if (!search_path(selected_cmd_exe, cmd_exe))
            return 0;
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
        return 0;

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
            remote = dup_handle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(handles[index_remote])));

            _close(handles[index_remote]);
            if (!local)
                _close(handles[index_local]);
        }

        if (local && remote)
            return true;

        if (local) fclose(local);
        if (remote) CloseHandle(remote);
        local = nullptr;
        remote = 0;
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
/// <span class="arg">mode</span> can be <code>"t"</code> for text mode (the
/// default if omitted) or <code>"b"</code> for binary mode.
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
static int io_popenrw(lua_State* state)
{
    bool failed = false;

    const char* command = luaL_checkstring(state, 1);
    const char* mode = luaL_optstring(state, 2, "t");
    failed = !command || !mode || ((mode[0] != 't' && mode[0] != 'b') || mode[1]);

    luaL_Stream* pr = nullptr;
    luaL_Stream* pw = nullptr;
    pipe_pair pipe_stdin;
    pipe_pair pipe_stdout;

    if (!failed)
    {
        pr = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
        luaL_setmetatable(state, LUA_FILEHANDLE);
        pr->f = nullptr;
        pr->closef = nullptr;

        pw = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
        luaL_setmetatable(state, LUA_FILEHANDLE);
        pw->f = nullptr;
        pw->closef = nullptr;

        bool binary = (mode[0] == 'b');
        failed = (!pipe_stdin.init(true/*write*/, binary) ||
                  !pipe_stdout.init(false/*write*/, binary));
    }

    if (!failed)
    {
        popenrw_info* info = new popenrw_info;
        intptr_t process_handle = popenrw_internal(command, pipe_stdin.remote, pipe_stdout.remote);

        if (!process_handle)
        {
            failed = true;
            lua_pop(state, 2);
            delete info;
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
void io_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "popenrw",     &io_popenrw },
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
