// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_bindable.h"
#include "yield.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <lib/doskey.h>
#include <process/process.h>
#include <sys/utime.h>
#include <ntverp.h> // for VER_PRODUCTMAJORVERSION to deduce SDK version
#include <assert.h>

extern "C" {
#include <lstate.h>
extern int _rl_match_hidden_files;
}

#include <memory>

//------------------------------------------------------------------------------
extern setting_bool g_files_hidden;
extern setting_bool g_files_system;

//------------------------------------------------------------------------------
extern int clink_is_signaled();

//------------------------------------------------------------------------------
extern "C" void __cdecl __acrt_errno_map_os_error(unsigned long const oserrno);
static void map_errno() { __acrt_errno_map_os_error(GetLastError()); }
static void map_errno(unsigned long const oserrno) { __acrt_errno_map_os_error(oserrno); }

//------------------------------------------------------------------------------
static int lua_osboolresult(lua_State *state, bool stat, const char *tag=nullptr)
{
    int en = errno;  /* calls to Lua API may change this value */

    lua_pushboolean(state, stat);

    if (stat)
        return 1;

    if (tag)
        lua_pushfstring(state, "%s: %s", tag, strerror(en));
    else
        lua_pushfstring(state, "%s", strerror(en));
    lua_pushinteger(state, en);
    return 3;
}

//------------------------------------------------------------------------------
static int lua_osstringresult(lua_State *state, const char* result, bool stat, const char *tag=nullptr)
{
    int en = errno;  /* calls to Lua API may change this value */

    if (stat)
    {
        lua_pushstring(state, result);
        return 1;
    }

    lua_pushnil(state);
    if (tag)
        lua_pushfstring(state, "%s: %s", tag, strerror(en));
    else
        lua_pushfstring(state, "%s", strerror(en));
    lua_pushinteger(state, en);
    return 3;
}



//------------------------------------------------------------------------------
static int close_file(lua_State *state)
{
    luaL_Stream* p = ((luaL_Stream*)luaL_checkudata(state, 1, LUA_FILEHANDLE));
    assert(p);
    if (!p)
        return 0;

    int res = fclose(p->f);
    return luaL_fileresult(state, (res == 0), NULL);
}



//------------------------------------------------------------------------------
struct glob_flags
{
    bool hidden = true;
    bool system = false;
};

//------------------------------------------------------------------------------
class globber_lua
    : public lua_bindable<globber_lua>
{
public:
                        globber_lua(const char* pattern, int extrainfo, const glob_flags& flags, bool dirs_only, bool back_compat=false);
    int                 next(lua_State* state);
    int                 close(lua_State* state);

private:
    globber             m_globber;
    str<288>            m_parent;
    int                 m_extrainfo;
    int                 m_index = 1;

    friend class lua_bindable<globber_lua>;
    static const char* const c_name;
    static const method c_methods[];
};

//------------------------------------------------------------------------------
const char* const globber_lua::c_name = "globber_lua";
const globber_lua::method globber_lua::c_methods[] = {
    { "next",                   &next },
    { "close",                  &close },
    {}
};

//------------------------------------------------------------------------------
globber_lua::globber_lua(const char* pattern, int extrainfo, const glob_flags& flags, bool dirs_only, bool back_compat)
: m_globber(pattern)
, m_parent(pattern)
, m_extrainfo(extrainfo)
{
    path::to_parent(m_parent, nullptr);

    m_globber.files(!dirs_only);
    m_globber.hidden(flags.hidden);
    m_globber.system(flags.system);
    if (back_compat)
        m_globber.suffix_dirs(false);
}

//------------------------------------------------------------------------------
static bool glob_next(lua_State* state, globber& globber, str_base& parent, int& index, int extrainfo);
int globber_lua::next(lua_State* state)
{
    // Arg is table into which to glob files/dirs; glob_next appends into it.

    const DWORD ms_max = 20;
    const DWORD num_max = 250;
    const DWORD tick = GetTickCount();

    bool ret = false;
    for (size_t c = 0; c < num_max; c++)
    {
        ret = glob_next(state, m_globber, m_parent, m_index, m_extrainfo);
        if (!ret)
            break;
        if (GetTickCount() - tick > ms_max)
            break;
    }

    lua_pushboolean(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
int globber_lua::close(lua_State* state)
{
    m_globber.close();
    return 0;
}



//------------------------------------------------------------------------------
struct execute_thread : public yield_thread
{
                    execute_thread(const char* command) : m_command(command) {}
                    ~execute_thread() {}
    int             results(lua_State* state) override;
private:
    void            do_work() override;
    str_moveable    m_command;
    int             m_stat = -1;
    errno_t         m_errno = 0;
};

//------------------------------------------------------------------------------
void execute_thread::do_work()
{
    m_stat = os::system(m_command.c_str(), get_cwd());
    m_errno = errno;
}

//------------------------------------------------------------------------------
int execute_thread::results(lua_State* state)
{
    errno = m_errno;
    return luaL_execresult(state, m_stat);
}



//------------------------------------------------------------------------------
static class delay_load_version
{
public:
                        delay_load_version();
    bool                init();
    DWORD               GetFileVersionInfoSizeW(LPCWSTR lpstrFilename, LPDWORD lpdwHandle);
    BOOL                GetFileVersionInfoW(LPCWSTR lpstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
    BOOL                VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[3];
        struct {
            DWORD (WINAPI* GetFileVersionInfoSizeW)(LPCWSTR lpstrFilename, LPDWORD lpdwHandle);
            BOOL (WINAPI* GetFileVersionInfoW)(LPCWSTR lpstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
            BOOL (WINAPI* VerQueryValueW)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
        };
    } m_procs;
} s_version;

//------------------------------------------------------------------------------
delay_load_version::delay_load_version()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_version::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        HMODULE hlib = LoadLibrary("version.dll");
        if (hlib)
        {
            m_procs.proc[0] = GetProcAddress(hlib, "GetFileVersionInfoSizeW");
            m_procs.proc[1] = GetProcAddress(hlib, "GetFileVersionInfoW");
            m_procs.proc[2] = GetProcAddress(hlib, "VerQueryValueW");
        }

        m_ok = true;
        for (auto const& proc : m_procs.proc)
        {
            if (!proc)
            {
                m_ok = false;
                break;
            }
        }
    }

    return m_ok;
}

//------------------------------------------------------------------------------
DWORD delay_load_version::GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
    if (!init())
        return 0;
    return m_procs.GetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}

//------------------------------------------------------------------------------
BOOL delay_load_version::GetFileVersionInfoW(LPCWSTR lpstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    return init() && m_procs.GetFileVersionInfoW(lpstrFilename, dwHandle, dwLen, lpData);
}

//------------------------------------------------------------------------------
BOOL delay_load_version::VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
    return init() && m_procs.VerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);
}



//------------------------------------------------------------------------------
/// -name:  os.chdir
/// -ver:   1.0.0
/// -arg:   path:string
/// -ret:   boolean
/// Changes the current directory to <span class="arg">path</span> and returns
/// whether it was successful.
/// If unsuccessful it returns false, an error message, and the error number.
//------------------------------------------------------------------------------
/// -name:  clink.chdir
/// -deprecated: os.chdir
/// -arg:   path:string
/// -ret:   boolean
int set_current_dir(lua_State* state)
{
    const char* dir = checkstring(state, 1);
    if (!dir)
        return 0;

    bool ok = os::set_current_dir(dir);
    return lua_osboolresult(state, ok, dir);
}

//------------------------------------------------------------------------------
/// -name:  os.getcwd
/// -ver:   1.0.0
/// -ret:   string
/// Returns the current directory.
//------------------------------------------------------------------------------
/// -name:  clink.get_cwd
/// -deprecated: os.getcwd
/// -ret:   string
int get_current_dir(lua_State* state)
{
    str<288> dir;
    os::get_current_dir(dir);

    lua_pushlstring(state, dir.c_str(), dir.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.mkdir
/// -ver:   1.0.0
/// -arg:   path:string
/// -ret:   boolean
/// Creates the directory <span class="arg">path</span> and returns whether it
/// was successful.
/// If unsuccessful it returns false, an error message, and the error number.
static int make_dir(lua_State* state)
{
    const char* dir = checkstring(state, 1);
    if (!dir)
        return 0;

    bool ok = os::make_dir(dir);
    return lua_osboolresult(state, ok, dir);
}

//------------------------------------------------------------------------------
/// -name:  os.rmdir
/// -ver:   1.0.0
/// -arg:   path:string
/// -ret:   boolean
/// Removes the directory <span class="arg">path</span> and returns whether it
/// was successful.
/// If unsuccessful it returns false, an error message, and the error number.
static int remove_dir(lua_State* state)
{
    const char* dir = checkstring(state, 1);
    if (!dir)
        return 0;

    bool ok = os::remove_dir(dir);
    return lua_osboolresult(state, ok, dir);
}

//------------------------------------------------------------------------------
/// -name:  os.isdir
/// -ver:   1.0.0
/// -arg:   path:string
/// -ret:   boolean
/// Returns whether <span class="arg">path</span> is a directory.
int is_dir(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_dir));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.isfile
/// -ver:   1.0.0
/// -arg:   path:string
/// -ret:   boolean
/// Returns whether <span class="arg">path</span> is a file.
static int is_file(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    lua_pushboolean(state, (os::get_path_type(path) == os::path_type_file));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getdrivetype
/// -ver:   1.3.37
/// -arg:   path:string
/// -ret:   string
/// Returns the drive type for the drive associated with the specified
/// <span class="arg">path</span>.
///
/// Relative paths automatically use the current drive.  Absolute paths use the
/// specified drive.  UNC paths are always reported as remote drives.
///
/// The possible drive types are:
/// <p><table>
/// <tr><th>Type</th><th>Description</th></tr>
/// <tr><td>"unknown"</td><td>The drive type could not be determined.</td></tr>
/// <tr><td>"invalid"</td><td>The drive type is invalid; for example, there is no volume mounted at the specified path.</td></tr>
/// <tr><td>"removable"</td><td>Floppy disk drive, thumb drive, flash card reader, CD-ROM, etc.</td></tr>
/// <tr><td>"fixed"</td><td>Hard drive, solid state drive, etc.</td></tr>
/// <tr><td>"ramdisk"</td><td>RAM disk.</td></tr>
/// <tr><td>"remote"</td><td>Remote (network) drive.</td></tr>
/// </table></p>
/// -show:  local t = os.getdrivetype("c:")
/// -show:  if t == "remote" then
/// -show:  &nbsp;   -- Network paths are often slow, and code may want to detect and skip them.
/// -show:  end
static int get_drive_type(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    int type = os::drive_type_unknown;
    if (path::is_unc(path))
    {
        type = os::drive_type_remote;
    }
    else
    {
        str<> full;
        if (os::get_full_path_name(path, full))
        {
            path::get_drive(full);
            path::append(full, ""); // Because get_drive_type() requires a trailing path separator.
            type = os::get_drive_type(path);
        }
    }

    const char* ret;
    switch (type)
    {
    default:
        assert(false);
        // fallthrough...
    case os::drive_type_unknown:    ret = "unknown"; break;
    case os::drive_type_invalid:    ret = "invalid"; break;
    case os::drive_type_remote:     ret = "remote"; break;      // Remote (network) drive.
    case os::drive_type_removable:  ret = "removable"; break;   // Floppy, thumb drive, flash card reader, CD-ROM, etc.
    case os::drive_type_fixed:      ret = "fixed"; break;       // Hard drive, flash drive, etc.
    case os::drive_type_ramdisk:    ret = "ramdisk"; break;     // RAM disk.
    }

    lua_pushstring(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.ishidden
/// -deprecated: os.globfiles
/// -arg:   path:string
/// -ret:   boolean
/// Returns whether <span class="arg">path</span> has the hidden attribute set.
static int is_hidden(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    lua_pushboolean(state, os::is_hidden(path));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.unlink
/// -ver:   1.0.0
/// -arg:   path:string
/// -ret:   boolean
/// Deletes the file <span class="arg">path</span> and returns whether it was
/// successful.
/// If unsuccessful it returns false, an error message, and the error number.
static int unlink(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    bool ok = os::unlink(path);
    return lua_osboolresult(state, ok, path);
}

//------------------------------------------------------------------------------
/// -name:  os.move
/// -ver:   1.0.0
/// -arg:   src:string
/// -arg:   dest:string
/// -ret:   boolean
/// Moves the <span class="arg">src</span> file to the
/// <span class="arg">dest</span> file.
/// If unsuccessful it returns false, an error message, and the error number.
static int move(lua_State* state)
{
    const char* src = checkstring(state, 1);
    const char* dest = checkstring(state, 2);
    if (!src || !dest)
        return 0;

    bool ok = os::move(src, dest);
    return lua_osboolresult(state, ok);
}

//------------------------------------------------------------------------------
/// -name:  os.copy
/// -ver:   1.0.0
/// -arg:   src:string
/// -arg:   dest:string
/// -ret:   boolean
/// Copies the <span class="arg">src</span> file to the
/// <span class="arg">dest</span> file.
/// If unsuccessful it returns false, an error message, and the error number.
static int copy(lua_State* state)
{
    const char* src = checkstring(state, 1);
    const char* dest = checkstring(state, 2);
    if (!src || !dest)
        return 0;

    bool ok = os::copy(src, dest);
    return lua_osboolresult(state, ok);
}

//------------------------------------------------------------------------------
static void add_type_tag(str_base& out, const char* tag)
{
    if (out.length())
        out << ",";
    out << tag;
}

//------------------------------------------------------------------------------
static bool glob_next(lua_State* state, globber& globber, str_base& parent, int& index, int extrainfo)
{
    str<288> file;
    globber::extrainfo info;
    globber::extrainfo* info_ptr = extrainfo ? &info : nullptr;
    if (!globber.next(file, false, info_ptr))
        return false;

    if (!extrainfo)
    {
        lua_pushlstring(state, file.c_str(), file.length());
    }
    else
    {
        lua_createtable(state, 0, 2);

        lua_pushliteral(state, "name");
        lua_pushlstring(state, file.c_str(), file.length());
        lua_rawset(state, -3);

        str<32> type;
        add_type_tag(type, (info.attr & FILE_ATTRIBUTE_DIRECTORY) ? "dir" : "file");
#ifdef S_ISLNK
        if (S_ISLNK(info.st_mode))
        {
            unsigned int len = parent.length();
            path::append(parent, file.c_str());

            add_type_tag(type, "link");
            wstr<288> wfile(parent.c_str());
            struct _stat64 st;
            if (_wstat64(wfile.c_str(), &st) < 0)
                add_type_tag(type, "orphaned");

            parent.truncate(len);
        }
#endif
        if (info.attr & FILE_ATTRIBUTE_HIDDEN)
            add_type_tag(type, "hidden");
        if (info.attr & FILE_ATTRIBUTE_SYSTEM)
            add_type_tag(type, "system");
        if (info.attr & FILE_ATTRIBUTE_READONLY)
            add_type_tag(type, "readonly");

        lua_pushliteral(state, "type");
        lua_pushlstring(state, type.c_str(), type.length());
        lua_rawset(state, -3);

        if (extrainfo >= 2)
        {
            lua_pushliteral(state, "atime");
            lua_pushnumber(state, lua_Number(os::filetime_to_time_t(info.accessed)));
            lua_rawset(state, -3);

            lua_pushliteral(state, "mtime");
            lua_pushnumber(state, lua_Number(os::filetime_to_time_t(info.modified)));
            lua_rawset(state, -3);

            lua_pushliteral(state, "ctime");
            lua_pushnumber(state, lua_Number(os::filetime_to_time_t(info.created)));
            lua_rawset(state, -3);

            lua_pushliteral(state, "size");
            lua_pushnumber(state, lua_Number(info.size));
            lua_rawset(state, -3);
        }
    }

    lua_rawseti(state, -2, index++);
    return true;
}

//------------------------------------------------------------------------------
static void get_glob_flags(lua_State* state, int index, glob_flags& out, bool back_compat)
{
    if (back_compat)
    {
        out.hidden = g_files_hidden.get() && _rl_match_hidden_files;
        out.system = g_files_system.get();
    }
    else if (lua_istable(state, index))
    {
#ifdef DEBUG
        int top = lua_gettop(state);
#endif

        lua_pushvalue(state, index);

        lua_pushliteral(state, "hidden");
        lua_rawget(state, -2);
        if (!lua_isnoneornil(state, -1))
            out.hidden = !!lua_toboolean(state, -1);
        lua_pop(state, 1);

        lua_pushliteral(state, "system");
        lua_rawget(state, -2);
        if (!lua_isnoneornil(state, -1))
            out.system = !!lua_toboolean(state, -1);
        lua_pop(state, 1);

        lua_pop(state, 1);

#ifdef DEBUG
        assert(lua_gettop(state) == top);
#endif
    }
}

//------------------------------------------------------------------------------
int glob_impl(lua_State* state, bool dirs_only, bool back_compat=false)
{
    const char* mask = checkstring(state, 1);
    if (!mask)
        return 0;

    int extrainfo;
    if (back_compat)
        extrainfo = 0;
    else if (lua_isboolean(state, 2))
        extrainfo = lua_toboolean(state, 2);
    else
        extrainfo = optinteger(state, 2, 0);

    glob_flags flags;
    get_glob_flags(state, 3, flags, back_compat);

    lua_createtable(state, 0, 0);

    globber globber(mask);
    globber.files(!dirs_only);
    globber.hidden(flags.hidden);
    globber.system(flags.system);
    if (back_compat)
        globber.suffix_dirs(false);

    str_moveable tmp(mask);
    path::to_parent(tmp, nullptr);

    int i = 1;
    if (back_compat)
    {
        while (true)
            if (!glob_next(state, globber, tmp, i, extrainfo))
                break;
    }
    else
    {
        while (true)
        {
            if (!glob_next(state, globber, tmp, i, extrainfo))
                break;
            if (!(i & 0x03) && clink_is_signaled())
                break;
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
int globber_impl(lua_State* state, bool dirs_only, bool back_compat=false)
{
    const char* mask = checkstring(state, 1);
    if (!mask)
        return 0;

    int extrainfo;
    if (back_compat)
        extrainfo = 0;
    else if (lua_isboolean(state, 2))
        extrainfo = lua_toboolean(state, 2);
    else
        extrainfo = optinteger(state, 2, 0);

    glob_flags flags;
    get_glob_flags(state, 3, flags, back_compat);

    if (!globber_lua::make_new(state, mask, extrainfo, flags, dirs_only, back_compat))
        return 0;

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.globdirs
/// -ver:   1.0.0
/// -arg:   globpattern:string
/// -arg:   [extrainfo:integer|boolean]
/// -arg:   [flags:table]
/// -ret:   table
/// Collects directories matching <span class="arg">globpattern</span> and
/// returns them in a table of strings.
///
/// The optional <span class="arg">extrainfo</span> argument can return a table
/// of tables instead, where each sub-table corresponds to one directory and has
/// the following scheme:
/// -show:  local t = os.globdirs(pattern, extrainfo)
/// -show:  -- Included when extrainfo is true or >= 1 (requires v1.1.7 or higher):
/// -show:  --   t[index].name      -- [string] The directory name.
/// -show:  --   t[index].type      -- [string] The match type (see below).
/// -show:  -- Included when extrainfo is 2 (requires v1.2.31 or higher):
/// -show:  --   t[index].size      -- [number] The file size, in bytes.
/// -show:  --   t[index].atime     -- [number] The access time, compatible with os.time().
/// -show:  --   t[index].mtime     -- [number] The modified time, compatible with os.time().
/// -show:  --   t[index].ctime     -- [number] The creation time, compatible with os.time().
/// The <span class="tablescheme">type</span> string is "dir", and may also
/// contain ",hidden", ",readonly", ",link", and ",orphaned" depending on the
/// attributes (making it usable as a match type for
/// <a href="#builder:addmatch">builder:addmatch()</a>).
///
/// Note: any quotation marks (<code>"</code>) in
/// <span class="arg">globpattern</span> are stripped.
///
/// Starting in v1.3.1, when this is used in a coroutine it automatically yields
/// periodically.
///
/// Starting in v1.4.16, the optional <span class="arg">flags</span> argument
/// can be a table with fields that select how directory globbing should behave.
/// By default hidden directories are included and system directories are
/// omitted.
/// -show:  local flags = {
/// -show:      hidden = false,     // True includes hidden directories, or false omits them.
/// -show:      system = true,      // True includes system directories, or false omits them.
/// -show:  }
/// -show:  local t = os.globdirs("*", true, flags)
int glob_dirs(lua_State* state)
{
    return glob_impl(state, true);
}

//------------------------------------------------------------------------------
/// -name:  os.globfiles
/// -ver:   1.0.0
/// -arg:   globpattern:string
/// -arg:   [extrainfo:integer|boolean]
/// -arg:   [flags:table]
/// -ret:   table
/// Collects files and/or directories matching
/// <span class="arg">globpattern</span> and returns them in a table of strings.
///
/// The optional <span class="arg">extrainfo</span> argument can return a table
/// of tables instead, where each sub-table corresponds to one file or directory
/// and has the following scheme:
/// -show:  local t = os.globfiles(pattern, extrainfo)
/// -show:  -- Included when extrainfo is true or >= 1 (requires v1.1.7 or higher):
/// -show:  --   t[index].name      -- [string] The file or directory name.
/// -show:  --   t[index].type      -- [string] The match type (see below).
/// -show:  -- Included when extrainfo is 2 (requires v1.2.31 or higher):
/// -show:  --   t[index].size      -- [number] The file size, in bytes.
/// -show:  --   t[index].atime     -- [number] The access time, compatible with os.time().
/// -show:  --   t[index].mtime     -- [number] The modified time, compatible with os.time().
/// -show:  --   t[index].ctime     -- [number] The creation time, compatible with os.time().
/// The <span class="tablescheme">type</span> string can be "file" or "dir", and
/// may also contain ",hidden", ",readonly", ",link", and ",orphaned" depending
/// on the attributes (making it usable as a match type for
/// <a href="#builder:addmatch">builder:addmatch()</a>).
///
/// Note: any quotation marks (<code>"</code>) in
/// <span class="arg">globpattern</span> are stripped.
///
/// Starting in v1.3.1, when this is used in a coroutine it automatically yields
/// periodically.
///
/// Starting in v1.4.16, the optional <span class="arg">flags</span> argument
/// can be a table with fields that select how file globbing should behave.  By
/// default hidden files are included and system files are omitted.
/// -show:  local flags = {
/// -show:      hidden = false,     // True includes hidden files, or false omits them.
/// -show:      system = true,      // True includes system files, or false omits them.
/// -show:  }
/// -show:  local t = os.globfiles("*", true, flags)
int glob_files(lua_State* state)
{
    return glob_impl(state, false);
}

//------------------------------------------------------------------------------
int make_dir_globber(lua_State* state)
{
    return globber_impl(state, true);
}

//------------------------------------------------------------------------------
int make_file_globber(lua_State* state)
{
    return globber_impl(state, false);
}

//------------------------------------------------------------------------------
/// -name:  os.touch
/// -ver:   1.2.31
/// -arg:   path:string
/// -arg:   [atime:number]
/// -arg:   [mtime:number]
/// -ret:   boolean
/// Sets the access and modified times for <span class="arg">path</span>, and
/// returns whether it was successful.
/// If unsuccessful it returns false, an error message, and the error number.
///
/// The second argument is <span class="arg">atime</span> and is a time to set
/// as the file's access time.  If omitted, the current time is used.  If
/// present, the value must use the same format as
/// <code><a href="https://www.lua.org/manual/5.2/manual.html#pdf-os.time">os.time()</a></code>.
///
/// The third argument is <span class="arg">mtime</span> and is a time to set as
/// the file's modified time.  If omitted, the <span class="arg">atime</span>
/// value is used (or the current time).  If present, the value must use the
/// same format as <code>os.time()</code>.  In order to pass
/// <span class="arg">mtime</span> it is necessary to also pass
/// <span class="arg">atime</span>.
int touch(lua_State* state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    struct utimbuf utb;
    struct utimbuf* ptr;

    if (lua_gettop(state) == 1)
    {
        // Passing nullptr uses the current time.
        ptr = nullptr;
    }
    else
    {
        utb.actime = static_cast<time_t>(optnumber(state, 2, 0));
        utb.modtime = static_cast<time_t>(optnumber(state, 3, lua_Number(utb.actime)));
        ptr = &utb;
    }

    int result = utime(path, ptr);
    return lua_osboolresult(state, result >= 0, path);
}

//------------------------------------------------------------------------------
/// -name:  os.getenv
/// -ver:   1.0.0
/// -arg:   name:string
/// -ret:   string | nil
/// Returns the value of the named environment variable, or nil if it doesn't
/// exist.
///
/// Note: Certain environment variable names receive special treatment:
///
/// <table>
/// <tr><th>Name</th><th>Special Behavior</th></tr>
/// <tr><td><code>"CD"</code></td><td>If %CD% is not set then a return value
///     is synthesized from the current working directory path name.</td></tr>
/// <tr><td><code>"CMDCMDLINE"</code></td><td>If %CMDCMDLINE% is not set then
///     this returns the command line that started the CMD process.</td></tr>
/// <tr><td><code>"ERRORLEVEL"</code></td><td>If %ERRORLEVEL% is not set and the
///     <code><a href="#cmd_get_errorlevel">cmd.get_errorlevel</a></code>
///     setting is enabled this returns the most recent exit code, just like the
///     <code>echo %ERRORLEVEL%</code> command displays.  Otherwise this returns
///     0.</td></tr>
/// <tr><td><code>"HOME"</code></td><td>If %HOME% is not set then a return value
///     is synthesized from %HOMEDRIVE% and %HOMEPATH%, or from
///     %USERPROFILE%.</td></tr>
/// <tr><td><code>"RANDOM"</code></td><td>If %RANDOM% is not set then this
///     returns a random integer.</td></tr>
/// </table>
int get_env(lua_State* state)
{
    // Some cmder-powerline-prompt scripts pass nil.  Allow nil for backward
    // compatibility.
    const char* name = optstring(state, 1, "");
    if (!name || !*name)
        return 0;

    str<128> value;
    if (!os::get_env(name, value))
        return 0;

    lua_pushlstring(state, value.c_str(), value.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.setenv
/// -ver:   1.0.0
/// -arg:   name:string
/// -arg:   value:string
/// -ret:   boolean
/// Sets the <span class="arg">name</span> environment variable to
/// <span class="arg">value</span> and returns whether it was successful.
/// If unsuccessful it returns false, an error message, and the error number.
int set_env(lua_State* state)
{
    const char* name = checkstring(state, 1);
    const char* value = optstring(state, 2, reinterpret_cast<const char*>(-1));
    if (!name || !value)
        return 0;

    if (value == reinterpret_cast<const char*>(-1))
        value = nullptr;

    bool ok = os::set_env(name, value);
    return lua_osboolresult(state, ok);
}

//------------------------------------------------------------------------------
/// -name:  os.expandenv
/// -ver:   1.2.5
/// -arg:   value:string
/// -ret:   string
/// Returns <span class="arg">value</span> with any <code>%name%</code>
/// environment variables expanded.  Names are case insensitive.  Special CMD
/// syntax is not supported (e.g. <code>%name:str1=str2%</code> or
/// <code>%name:~offset,length%</code>).
///
/// Note: See <a href="#os.getenv">os.getenv()</a> for a list of special
/// variable names.
int expand_env(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    str<280> out;
    os::expand_env(in, static_cast<unsigned int>(strlen(in)), out);

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getenvnames
/// -ver:   1.0.0
/// -ret:   table
/// Returns all environment variables in a table with the following scheme:
/// -show:  local t = os.getenvnames()
/// -show:  -- t[index].name        [string] The environment variable's name.
/// -show:  -- t[index].value       [string] The environment variable's value.
int get_env_names(lua_State* state)
{
    lua_createtable(state, 0, 0);

    WCHAR* root = GetEnvironmentStringsW();
    if (root == nullptr)
        return 1;

    str<128> var;
    WCHAR* strings = root;
    int i = 1;
    while (*strings)
    {
        // Skip env vars that start with a '='. They're hidden ones.
        if (*strings == '=')
        {
            strings += wcslen(strings) + 1;
            continue;
        }

        WCHAR* eq = wcschr(strings, '=');
        if (eq == nullptr)
            break;

        *eq = '\0';
        var = strings;

        lua_pushlstring(state, var.c_str(), var.length());
        lua_rawseti(state, -2, i++);

        ++eq;
        strings = eq + wcslen(eq) + 1;
    }

    FreeEnvironmentStringsW(root);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.gethost
/// -ver:   1.0.0
/// -ret:   string
/// Returns the fully qualified file name of the host process.  Currently only
/// CMD.EXE can host Clink.
static int get_host(lua_State* state)
{
    WCHAR module[280];
    DWORD len = GetModuleFileNameW(nullptr, module, sizeof_array(module));
    if (!len || len >= sizeof_array(module))
        return 0;

    str<280> host;
    host = module;

    lua_pushlstring(state, host.c_str(), host.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.geterrorlevel
/// -ver:   1.2.14
/// -ret:   integer
/// Returns the last command's exit code, if the
/// <code><a href="#cmd_get_errorlevel">cmd.get_errorlevel</a></code> setting is
/// enabled.  Otherwise it returns 0.
static int get_errorlevel(lua_State* state)
{
    lua_pushinteger(state, os::get_errorlevel());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getalias
/// -ver:   1.1.4
/// -arg:   name:string
/// -ret:   string | nil
/// Returns command string for doskey alias <span class="arg">name</span>, or
/// nil if the named alias does not exist.
int get_alias(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (!name)
        return 0;

    str<> out;
    if (!os::get_alias(name, out))
        return luaL_fileresult(state, false, name);

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getaliases
/// -ver:   1.0.0
/// -ret:   table
/// Returns doskey alias names in a table of strings.
int get_aliases(lua_State* state)
{
    lua_createtable(state, 0, 0);

    // Not const because Windows' alias API won't accept it.
    wchar_t* shell_name = const_cast<wchar_t*>(os::get_shellname());

    // Get the aliases (aka. doskey macros).
    int buffer_size = GetConsoleAliasesLengthW(shell_name);
    if (buffer_size == 0)
        return 1;

    // Don't use wstr<> because it only uses 15 bits to store the buffer size.
    buffer_size++;
    std::unique_ptr<WCHAR[]> buffer = std::unique_ptr<WCHAR[]>(new WCHAR[buffer_size]);
    if (!buffer)
        return 1;

    ZeroMemory(buffer.get(), buffer_size * sizeof(WCHAR));    // Avoid race condition!
    if (GetConsoleAliasesW(buffer.get(), buffer_size, shell_name) == 0)
        return 1;

    // Parse the result into a lua table.
    str<> out;
    WCHAR* alias = buffer.get();
    for (int i = 1; int(alias - buffer.get()) < buffer_size; ++i)
    {
        WCHAR* c = wcschr(alias, '=');
        if (c == nullptr)
            break;

        *c = '\0';
        out = alias;

        lua_pushlstring(state, out.c_str(), out.length());
        lua_rawseti(state, -2, i);

        ++c;
        alias = c + wcslen(c) + 1;
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.resolvealias
/// -ver:   1.3.12
/// -arg:   text:string
/// -ret:   table|nil
/// Identifies whether <span class="arg">text</span> begins with a doskey alias,
/// and expands the doskey alias.
///
/// Returns a table of strings, or nil if there is no associated doskey alias.
/// The return type is a table of strings because doskey aliases can be defined
/// to expand into multiple command lines:  one entry in the table per resolved
/// command line.  Most commonly, the table will contain one string.
int resolve_alias(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    doskey_alias out;
    doskey doskey("cmd.exe");
    doskey.resolve(in, out);

    if (!out)
        return 0;

    lua_createtable(state, 1, 0);

    str<> tmp;
    for (int i = 1; out.next(tmp); ++i)
    {
        lua_pushlstring(state, tmp.c_str(), tmp.length());
        lua_rawseti(state, -2, i);
    }

    return 1;
}

//------------------------------------------------------------------------------
int get_screen_info_impl(lua_State* state, bool back_compat)
{
    int i;
    int values[4];
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    values[0] = csbi.dwSize.X;
    values[1] = csbi.dwSize.Y;
    values[2] = csbi.srWindow.Right - csbi.srWindow.Left;
    values[3] = csbi.srWindow.Bottom - csbi.srWindow.Top;

    lua_createtable(state, 0, 4);
    {
        static const char* const newnames[] = {
            "bufwidth",
            "bufheight",
            "winwidth",
            "winheight",
        };

        static const char* const oldnames[] = {
            "buffer_width",
            "buffer_height",
            "window_width",
            "window_height",
        };

        static_assert(sizeof_array(values) == sizeof_array(newnames), "table sizes don't match");
        static_assert(sizeof_array(values) == sizeof_array(oldnames), "table sizes don't match");

        const char* const* names = back_compat ? oldnames : newnames;

        for (i = 0; i < sizeof_array(values); ++i)
        {
            lua_pushstring(state, names[i]);
            lua_pushinteger(state, values[i]);
            lua_rawset(state, -3);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getscreeninfo
/// -ver:   1.1.2
/// -ret:   table
/// Returns dimensions of the terminal's buffer and visible window. The returned
/// table has the following scheme:
/// -show:  local info = os.getscreeninfo()
/// -show:  -- info.bufwidth        [integer] Width of the screen buffer.
/// -show:  -- info.bufheight       [integer] Height of the screen buffer.
/// -show:  -- info.winwidth        [integer] Width of the visible window.
/// -show:  -- info.winheight       [integer] Height of the visible window.
static int get_screen_info(lua_State* state)
{
    return get_screen_info_impl(state, false);
}

//------------------------------------------------------------------------------
/// -name:  os.getbatterystatus
/// -ver:   1.1.17
/// -ret:   table
/// Returns a table containing the battery status for the device, or nil if an
/// error occurs.  The returned table has the following scheme:
/// -show:  local t = os.getbatterystatus()
/// -show:  -- t.level              [integer] The battery life from 0 to 100, or -1 if error or no battery.
/// -show:  -- t.acpower            [boolean] Whether the device is connected to AC power.
/// -show:  -- t.charging           [boolean] Whether the battery is charging.
/// -show:  -- t.batterysaver       [boolean] Whether Battery Saver mode is active.
int get_battery_status(lua_State* state)
{
    SYSTEM_POWER_STATUS status;
    if (!GetSystemPowerStatus(&status))
    {
        map_errno();
        return luaL_fileresult(state, false, nullptr);
    }

    int level = -1;
    if (status.BatteryLifePercent <= 100)
        level = status.BatteryLifePercent;
    if (status.BatteryFlag & 128)
        level = -1;

    lua_createtable(state, 0, 4);

    lua_pushliteral(state, "level");
    lua_pushinteger(state, level);
    lua_rawset(state, -3);

    lua_pushliteral(state, "acpower");
    lua_pushboolean(state, (status.ACLineStatus == 1));
    lua_rawset(state, -3);

    lua_pushliteral(state, "charging");
    lua_pushboolean(state, ((status.BatteryFlag & 0x88) == 0x08));
    lua_rawset(state, -3);

    lua_pushliteral(state, "batterysaver");
#if defined( VER_PRODUCTMAJORVERSION ) && VER_PRODUCTMAJORVERSION >= 10
    lua_pushboolean(state, ((status.SystemStatusFlag & 1) == 1));
#else
    lua_pushboolean(state, ((status.Reserved1 & 1) == 1));
#endif
    lua_rawset(state, -3);

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getpid
/// -ver:   1.1.41
/// -ret:   integer
/// Returns the CMD.EXE process ID. This is mainly intended to help with salting
/// unique resource names (for example named pipes).
int get_pid(lua_State* state)
{
    DWORD pid = GetCurrentProcessId();
    lua_pushinteger(state, pid);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.createtmpfile
/// -ver:   1.1.42
/// -ret:   file, string
/// -arg:   [prefix:string]
/// -arg:   [ext:string]
/// -arg:   [path:string]
/// -arg:   [mode:string]
/// Creates a uniquely named file, intended for use as a temporary file.  The
/// name pattern is "<em>location</em> <code>\</code> <em>prefix</em>
/// <code>_</code> <em>processId</em> <code>_</code> <em>uniqueNum</em>
/// <em>extension</em>".
///
/// <span class="arg">prefix</span> optionally specifies a prefix for the file
/// name and defaults to "tmp".
///
/// <span class="arg">ext</span> optionally specifies a suffix for the file name
/// and defaults to "" (if <span class="arg">ext</span> starts with a period "."
/// then it is a filename extension).
///
/// <span class="arg">path</span> optionally specifies a path location in which
/// to create the file.  The default is the system TEMP directory.
///
/// <span class="arg">mode</span> optionally specifies "t" for text mode (line
/// endings are translated) or "b" for binary mode (untranslated IO).  The
/// default is "t".
///
/// When successful, the function returns a file handle and the file name.
/// If unsuccessful it returns nil, an error message, and the error number.
///
/// <strong>Note:</strong> Be sure to delete the file when finished, or it will
/// be leaked.
static int create_tmp_file(lua_State *state)
{
    const char* prefix = optstring(state, 1, "");
    const char* ext = optstring(state, 2, "");
    const char* path = optstring(state, 3, "");
    const char* mode = optstring(state, 4, "t");
    if (!prefix || !ext || !path || !mode)
        return 0;
    if ((mode[0] != 'b' && mode[0] != 't') || mode[1])
        return luaL_error(state, "invalid mode " LUA_QS
                          " (use " LUA_QL("t") ", " LUA_QL("b") ", or nil)", mode);

    bool binary_mode = (mode[0] == 'b');

    str<> name;
    os::temp_file_mode tmode = binary_mode ? os::binary : os::normal;
    FILE* f = os::create_temp_file(&name, prefix, ext, tmode, path);

    if (!f)
        return luaL_fileresult(state, 0, 0);

    luaL_Stream* p = (luaL_Stream*)lua_newuserdata(state, sizeof(luaL_Stream));
    luaL_setmetatable(state, LUA_FILEHANDLE);
    p->f = f;
    p->closef = &close_file;

    lua_pushstring(state, name.c_str());
    return 2;
}

//------------------------------------------------------------------------------
/// -name:  os.getshortpathname
/// -ver:   1.1.42
/// -arg:   path:string
/// -ret:   string
/// Returns the 8.3 short path name for <span class="arg">path</span>.  This may
/// return the input path if an 8.3 short path name is not available.
/// If unsuccessful it returns nil, an error message, and the error number.
static int get_short_path_name(lua_State *state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> out;
    bool ok = os::get_short_path_name(path, out);
    return lua_osstringresult(state, out.c_str(), ok, path);
}

//------------------------------------------------------------------------------
/// -name:  os.getlongpathname
/// -ver:   1.1.42
/// -arg:   path:string
/// -ret:   string
/// Returns the long path name for <span class="arg">path</span>.
/// If unsuccessful it returns nil, an error message, and the error number.
static int get_long_path_name(lua_State *state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> out;
    bool ok = os::get_long_path_name(path, out);
    return lua_osstringresult(state, out.c_str(), ok, path);
}

//------------------------------------------------------------------------------
/// -name:  os.getfullpathname
/// -ver:   1.1.42
/// -arg:   path:string
/// -ret:   string
/// Returns the full path name for <span class="arg">path</span>.
/// If unsuccessful it returns nil, an error message, and the error number.
static int get_full_path_name(lua_State *state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> out;
    bool ok = os::get_full_path_name(path, out);
    return lua_osstringresult(state, out.c_str(), ok, path);
}

//------------------------------------------------------------------------------
/// -name:  os.gettemppath
/// -ver:   1.3.18
/// -ret:   string
/// Returns the path of the system temporary directory.
/// If unsuccessful it returns nil, an error message, and the error number.
static int get_temp_path(lua_State *state)
{
    str<> out;
    bool ok = os::get_temp_dir(out);
    return lua_osstringresult(state, out.c_str(), ok);
}

//------------------------------------------------------------------------------
/// -name:  os.getnetconnectionname
/// -ver:   1.2.27
/// -arg:   path:string
/// -ret:   string
/// Returns the remote name associated with <span class="arg">path</span>, or an
/// empty string if it's not a network drive.
/// If unsuccessful it returns nil, an error message, and the error number.
static int get_net_connection_name(lua_State *state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> out;
    bool ok = os::get_net_connection_name(path, out);
    return lua_osstringresult(state, out.c_str(), ok, path);
}

//------------------------------------------------------------------------------
/// -name:  os.debugprint
/// -ver:   1.2.20
/// -arg:   ...
/// This works like
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-print">print()</a>
/// but writes the output via the OS <code>OutputDebugString()</code> API.
///
/// This function has no effect if the
/// <code><a href="#lua_debug">lua.debug</a></code> Clink setting is off.
/// -show:  clink.debugprint("my variable = "..myvar)
static int debug_print(lua_State *state)
{
    str<> out;
    bool err = false;

    int n = lua_gettop(state);              // Number of arguments.
    lua_getglobal(state, "tostring");       // Function to convert to string (reused each loop iteration).

    for (int i = 1; i <= n; i++)
    {
        // Call function to convert arg to a string.
        lua_pushvalue(state, -1);           // Function to be called (tostring).
        lua_pushvalue(state, i);            // Value to print.
        if (lua_state::pcall(state, 1, 1) != 0)
            return 0;

        // Get result from the tostring call.
        size_t l;
        const char* s = lua_tolstring(state, -1, &l);
        if (s == NULL)
        {
            err = true;
            break;                          // Allow accumulated output to be printed before erroring out.
        }
        lua_pop(state, 1);                  // Pop result.

        // Add tab character to the output.
        if (i > 1)
            out << "\t";

        // Add string result to the output.
        out.concat(s, int(l));
    }

    out.concat("\r\n");
    OutputDebugStringA(out.c_str());
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  os.clock
/// -ver:   1.2.30
/// -ret:   number
/// This returns the number of seconds since the program started.
///
/// Normally, Lua's os.clock() has millisecond precision until the program has
/// been running for almost 25 days, and then it suddenly breaks and starts
/// always returning -0.001 seconds.
///
/// Clink's version of os.clock() has microsecond precision until the program
/// has been running for many weeks.  It maintains at least millisecond
/// precision until the program has been running for many years.
///
/// It was necessary to replace os.clock() in order for
/// <a href="#asyncpromptfiltering">asynchronous prompt filtering</a> to
/// continue working when CMD has been running for more than 25 days.
static int double_clock(lua_State *state)
{
    lua_Number elapsed = os::clock();
    lua_pushnumber(state, elapsed);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.getclipboardtext
/// -ver:   1.2.32
/// -ret:   string | nil
/// This returns the text from the system clipboard, or nil if there is no text
/// on the system clipboard.
static int get_clipboard_text(lua_State *state)
{
    str<1024> utf8;
    if (!os::get_clipboard_text(utf8))
        return 0;

    lua_pushlstring(state, utf8.c_str(), utf8.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.setclipboardtext
/// -arg:   string
/// -ver:   1.2.32
/// -ret:   boolean
/// This sets the text onto the system clipboard, and returns whether it was
/// successful.
static int set_clipboard_text(lua_State *state)
{
    const char* text = checkstring(state, 1);
    if (!text)
        return 0;

    bool ok = os::set_clipboard_text(text, int(strlen(text)));
    lua_pushboolean(state, ok);
    return 1;
}

//------------------------------------------------------------------------------
static int os_executeyield_internal(lua_State *state) // gcc can't handle 'friend' and 'static'.
{
    bool ismain = (G(state)->mainthread == state);
    const char *command = luaL_optstring(state, 1, NULL);
    if (ismain || command == nullptr)
    {
        assert(false);
        return 0;
    }

    luaL_YieldGuard* yg = luaL_YieldGuard::make_new(state);

    std::shared_ptr<execute_thread> thread = std::make_shared<execute_thread>(command);
    if (thread->createthread())
    {
        yg->init(thread, command);
        thread->go();
    }

    return 1; // yg
}

//------------------------------------------------------------------------------
/// -name:  os.issignaled
/// -ver:   1.3.14
/// -ret:   boolean
/// Returns whether a <kbb>Ctrl</kbd>+<kbd>Break</kbd> has been received.
/// Scripts may use this to decide when to end work early.
static int is_signaled(lua_State *state)
{
    lua_pushboolean(state, clink_is_signaled());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  os.sleep
/// -ver:   1.3.16
/// -arg:   seconds:number
/// Sleeps for the indicated duration, in seconds, with millisecond granularity.
/// -show:  os.sleep(0.01)  -- Sleep for 10 milliseconds.
static int sleep(lua_State *state)
{
    int isnum;
    double sec = lua_tonumberx(state, -1, &isnum);
    if (isnum)
    {
        const DWORD ms = DWORD(sec * 1000);
        Sleep(ms);
    }
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  os.expandabbreviatedpath
/// -ver:   1.4.1
/// -arg:   path:string
/// -ret:   nil | string, string, boolean
/// This expands any abbreviated directory components in
/// <span class="arg">path</span>.
///
/// The return value is nil if <span class="arg">path</span> couldn't be
/// expanded.  It can't be expanded if it is a UNC path or a remote drive, or if
/// it is already expanded, or if there are no matches for one of the directory
/// components.
///
/// Otherwise three values are returned.  First, a string containing the
/// expanded part of <span class="arg">path</span>.  Second, a string containing
/// the rest of <span class="arg">path</span> that wasn't expanded.  Third, a
/// boolean indicating whether <span class="arg">path</span> was able to expand
/// uniquely.
/// -show:  -- Suppose only the following directories exist in the D: drive:
/// -show:  --  - D:\bag
/// -show:  --  - D:\bookkeeper
/// -show:  --  - D:\bookkeeping
/// -show:  --  - D:\box
/// -show:  --  - D:\boxes
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\b\\file")
/// -show:  -- returns "d:\\b", "\\file", false         -- Ambiguous.
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\ba\\file")
/// -show:  -- returns "d:\\bag", "\\file", true        -- Unique; only "bag" can match "ba".
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\bo\\file")
/// -show:  -- returns "d:\\bo", "\\file", false        -- Ambiguous.
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\boo\\file")
/// -show:  -- returns "d:\\bookkeep", "\\file", false  -- Ambiguous; "bookkeep" is longest common part matching "boo".
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\box\\file")
/// -show:  -- returns "d:\\box", "\\file", false       -- Ambiguous; "box" is an exact match.
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\boxe\\file")
/// -show:  -- returns "d:\\boxes", "\\file", true      -- Unique; only "boxes" can match "boxe".
/// -show:
/// -show:  expanded, remaining, unique = os.expandabbreviatedpath("d:\\boxes\\file")
/// -show:  -- returns nil                              -- Is already expanded.
static int expand_abbreviated_path(lua_State *state)
{
    const char* path = checkstring(state, 1);
    if (!path)
        return 0;

    str<> expanded;
    const bool unique = os::disambiguate_abbreviated_path(path, expanded);

    if (!expanded.length())
    {
        lua_pushnil(state);
        return 1;
    }

    lua_pushlstring(state, expanded.c_str(), expanded.length());
    lua_pushstring(state, path);
    lua_pushboolean(state, unique);
    return 3;
}

//------------------------------------------------------------------------------
/// -name:  os.isuseradmin
/// -ver:   1.4.17
/// -ret:   boolean
/// Returns true if running as an administrator account.
static int is_user_admin(lua_State *state)
{
    const bool is = os::is_user_admin();
    lua_pushboolean(state, is);
    return 1;
}

//------------------------------------------------------------------------------
static bool maybe_rawset(lua_State *state, const char* name, const char* value)
{
    if (!value || !*value)
        return false;

    lua_pushstring(state, name);
    lua_pushstring(state, value);
    lua_rawset(state, -3);
    return true;
}

//------------------------------------------------------------------------------
/// -name:  os.getfileversion
/// -ver:   1.4.17
/// -arg:   file:string
/// -ret:   table | nil
/// This tries to get a Windows file version info resource from the specified
/// file.  It tries to get translated strings in the closest available language
/// to the current user language configured in the OS.
///
/// If successful, the returned table contains as many of the following fields
/// as were available in the file's version info resource.
/// -show:  local info = os.getfileversion("c:/windows/notepad.exe")
/// -show:  -- info.filename            c:\windows\notepad.exe
/// -show:  -- info.filevernum          10.0.19041.1865
/// -show:  -- info.productvernum       10.0.19041.1865
/// -show:  -- info.fileflags
/// -show:  -- info.osplatform          Windows NT
/// -show:  -- info.osqualifier
/// -show:  -- info.comments
/// -show:  -- info.companyname         Microsoft Corporation
/// -show:  -- info.filedescription     Notepad
/// -show:  -- info.fileversion         10.0.19041.1 (WinBuild.160101.0800)
/// -show:  -- info.internalname        Notepad
/// -show:  -- info.legalcopyright      © Microsoft Corporation. All rights reserved.
/// -show:  -- info.legaltrademarks
/// -show:  -- info.originalfilename    NOTEPAD.EXE.MUI
/// -show:  -- info.productname         Microsoft® Windows® Operating System
/// -show:  -- info.productversion      10.0.19041.1
/// -show:  -- info.privatebuild
/// -show:  -- info.specialbuild
/// Note:  The <span class="arg">fileflags</span> field may be nil (omitted),
/// or it may contain a table with additional fields.
/// -show:  if info.fileflags then
/// -show:  -- info.fileflags.debug
/// -show:  -- info.fileflags.prerelease
/// -show:  -- info.fileflags.patched
/// -show:  -- info.fileflags.privatebuild
/// -show:  -- info.fileflags.specialbuild
/// -show:  end
static int get_file_version(lua_State *state)
{
    const char* file = checkstring(state, 1);
    if (!file)
        return 0;

    if (!s_version.init())
        return 0;

    str<> full_file;
    str<> tmp;
    void* pv;
    UINT cb;

    os::get_full_path_name(file, full_file);

    globber globber(full_file.c_str());
    globber.directories(false);
    globber.hidden(true);
    globber.system(true);

    globber::extrainfo extra;
    if (!globber.next(tmp, true, &extra))
    {
os_error:
        map_errno();
os_stringresult:
        return lua_osstringresult(state, nullptr, false);
    }

    DWORD dwErr = 0;
    DWORD dwHandle;
    wstr<> wfile(full_file.c_str());
    const DWORD dwSize = s_version.GetFileVersionInfoSizeW(wfile.c_str(), &dwHandle);
    if (!dwSize)
    {
        dwErr = GetLastError();
        if (dwErr == ERROR_RESOURCE_DATA_NOT_FOUND ||
            dwErr == ERROR_RESOURCE_TYPE_NOT_FOUND ||
            dwErr == ERROR_RESOURCE_NAME_NOT_FOUND ||
            dwErr == ERROR_RESOURCE_LANG_NOT_FOUND)
            dwErr = ERROR_FILE_NOT_FOUND;
        map_errno(dwErr);
        goto os_stringresult;
    }

    autoptr<BYTE> verinfo(static_cast<BYTE*>(malloc(dwSize)));
    if (!verinfo.get())
    {
        map_errno(ERROR_OUTOFMEMORY);
        goto os_stringresult;
    }

    if (!s_version.GetFileVersionInfoW(wfile.c_str(), dwHandle, dwSize, verinfo.get()))
        goto os_error;

    lua_createtable(state, 0, 10);

    maybe_rawset(state, "filename", full_file.c_str());

    pv = nullptr;
    cb = 0;
    if (s_version.VerQueryValueW(verinfo.get(), L"\\", &pv, &cb) && pv && cb)
    {
        const VS_FIXEDFILEINFO* const pffi = reinterpret_cast<const VS_FIXEDFILEINFO*>(pv);

        tmp.format("%u.%u.%u.%u",
            HIWORD(pffi->dwFileVersionMS),
            LOWORD(pffi->dwFileVersionMS),
            HIWORD(pffi->dwFileVersionLS),
            LOWORD(pffi->dwFileVersionLS));
        maybe_rawset(state, "filevernum", tmp.c_str());

        tmp.format("%u.%u.%u.%u",
            HIWORD(pffi->dwProductVersionMS),
            LOWORD(pffi->dwProductVersionMS),
            HIWORD(pffi->dwProductVersionLS),
            LOWORD(pffi->dwProductVersionLS));
            maybe_rawset(state, "productvernum", tmp.c_str());

        struct FlagEntry
        {
            DWORD dw;
            const char* psz;
        };

        {
            static const FlagEntry c_file_flags[] =
            {
                { VS_FF_DEBUG, "debug" },
                { VS_FF_PRERELEASE, "prerelease" },
                { VS_FF_PATCHED, "patched" },
                { VS_FF_PRIVATEBUILD, "privatebuild" },
                { VS_FF_SPECIALBUILD, "specialbuild" },
            };

            lua_pushliteral(state, "fileflags");
            lua_createtable(state, 0, 5);

            bool any = false;
            for (unsigned int ii = 0; ii < _countof(c_file_flags); ++ii)
            {
                if ((c_file_flags[ii].dw & pffi->dwFileFlagsMask) &&
                    (c_file_flags[ii].dw & pffi->dwFileFlags))
                {
                    lua_pushstring(state, c_file_flags[ii].psz);
                    lua_pushboolean(state, true);
                    lua_rawset(state, -3);
                    any = true;
                }
            }

            if (any)
                lua_rawset(state, -3);
            else
                lua_pop(state, 2);
        }

        {
            static const FlagEntry c_platforms[] =
            {
                { VOS_DOS, "MSDOS" },
                { VOS_OS216, "16-bit OS/2" },
                { VOS_OS232, "32-bit OS/2" },
                { VOS_NT, "Windows NT" },
                { VOS_WINCE, "Windows CE" },
            };

            static const FlagEntry c_qualifiers[] =
            {
                { VOS__WINDOWS16, "16-bit Windows" },
                { VOS__PM16, "16-bit Presentation Manager" },
                { VOS__PM32, "32-bit Presentation Manager" },
                { VOS__WINDOWS32, "32-bit Windows" },
            };

            DWORD dwFileOS = pffi->dwFileOS;
            if (dwFileOS == VOS_NT_WINDOWS32)
                dwFileOS = VOS_NT;

            for (unsigned int ii = 0; ii < _countof(c_platforms); ++ii)
            {
                if (c_platforms[ii].dw == (dwFileOS & 0xffff0000))
                {
                    lua_pushliteral(state, "osplatform");
                    lua_pushstring(state, c_platforms[ii].psz);
                    lua_rawset(state, -3);
                    break;
                }
            }
            for (unsigned int ii = 0; ii < _countof(c_qualifiers); ++ii)
            {
                if (c_qualifiers[ii].dw == LOWORD(dwFileOS))
                {
                    lua_pushlightuserdata(state, "osqualifier");
                    lua_pushstring(state, c_qualifiers[ii].psz);
                    lua_rawset(state, -3);
                    break;
                }
            }
        }
    }

    // Get other predefined strings from translation string tables.  Yuck, what
    // a heuristic mess.

    struct LANGANDCODEPAGE
    {
        WORD wLanguage;
        WORD wCodePage;
    };

    UINT dwTranslationsSize;
    LANGANDCODEPAGE* pTranslations;
    if (s_version.VerQueryValueW(verinfo.get(), L"\\VarFileInfo\\Translation", (void**)&pTranslations, &dwTranslationsSize))
    {
        // If there are multiple codepage for a language ID, favor the first
        // codepage found, unless 1200 (UTF16) is found.
        const LANGID userLang = GetUserDefaultLangID();
        const LANGID systemLang = GetSystemDefaultLangID();
        const LANGID englishLang = 0x409;
        const WORD cpUnicode = 0x4b0;

        const LANGANDCODEPAGE* userBest = nullptr;
        const LANGANDCODEPAGE* systemBest = nullptr;
        const LANGANDCODEPAGE* englishBest = nullptr;
        const LANGANDCODEPAGE* neutralBest = nullptr;
        for (unsigned int cTranslations = dwTranslationsSize / sizeof(LANGANDCODEPAGE); cTranslations--; pTranslations++)
        {
            if (pTranslations->wLanguage == userLang)
            {
                if (!userBest || pTranslations->wCodePage == cpUnicode)
                    userBest = pTranslations;
            }
            else if (pTranslations->wLanguage == systemLang)
            {
                if (!systemBest || pTranslations->wCodePage == cpUnicode)
                    systemBest = pTranslations;
            }
            else if (pTranslations->wLanguage == englishLang)
            {
                if (!englishBest || pTranslations->wCodePage == cpUnicode)
                    englishBest = pTranslations;
            }
            else if (pTranslations->wLanguage == 0)
            {
                if (!neutralBest || pTranslations->wCodePage == cpUnicode)
                    neutralBest = pTranslations;
            }
        }

        const LANGANDCODEPAGE* const bestBest = (userBest ? userBest :
                                                    systemBest ? systemBest :
                                                    englishBest ? englishBest :
                                                    neutralBest);

        if (bestBest)
        {
            wstr<> translationPath;
            translationPath.format(L"\\StringFileInfo\\%04x%04x\\", bestBest->wLanguage, bestBest->wCodePage);
            const unsigned int len_translationPath = translationPath.length();

            static const struct {
                const WCHAR* stringName;
                const char* fieldName;
                DWORD requiredFlag;
            } c_info[] = {
                { L"Comments", "comments" },
                { L"CompanyName", "companyname" },
                { L"FileDescription", "filedescription" },
                { L"FileVersion", "fileversion" },
                { L"InternalName", "internalname" },
                { L"LegalCopyright", "legalcopyright" },
                { L"LegalTrademarks", "legaltrademarks" },
                { L"OriginalFilename", "originalfilename" },
                { L"PrivateBuild", "privatebuild", VS_FF_PRIVATEBUILD },
                { L"ProductName", "productname" },
                { L"ProductVersion", "productversion" },
                { L"SpecialBuild", "specialbuild", VS_FF_SPECIALBUILD },
            };

            for (const auto& info : c_info)
            {
                pv = nullptr;
                cb = 0;
                translationPath.truncate(len_translationPath);
                translationPath.concat(info.stringName);
                if (s_version.VerQueryValueW(verinfo.get(), translationPath.c_str(), &pv, &cb) && pv && cb)
                {
                    tmp = static_cast<WCHAR*>(pv);
                    maybe_rawset(state, info.fieldName, tmp.c_str());
                }
            }
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
void os_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "chdir",       &set_current_dir },
        { "getcwd",      &get_current_dir },
        { "mkdir",       &make_dir },
        { "rmdir",       &remove_dir },
        { "isdir",       &is_dir },
        { "isfile",      &is_file },
        { "getdrivetype", &get_drive_type },
        { "ishidden",    &is_hidden },
        { "unlink",      &unlink },
        { "move",        &move },
        { "copy",        &copy },
        { "touch",       &touch },
        { "getenv",      &get_env },
        { "setenv",      &set_env },
        { "expandenv",   &expand_env },
        { "getenvnames", &get_env_names },
        { "gethost",     &get_host },
        { "geterrorlevel", &get_errorlevel },
        { "getalias",    &get_alias },
        { "getaliases",  &get_aliases },
        { "resolvealias", &resolve_alias },
        { "getscreeninfo", &get_screen_info },
        { "getbatterystatus", &get_battery_status },
        { "getpid",      &get_pid },
        { "createtmpfile", &create_tmp_file },
        { "getshortpathname", &get_short_path_name },
        { "getlongpathname", &get_long_path_name },
        { "getfullpathname", &get_full_path_name },
        { "gettemppath", &get_temp_path },
        { "getnetconnectionname", &get_net_connection_name },
        { "debugprint",  &debug_print },
        { "clock",       &double_clock },
        { "getclipboardtext", &get_clipboard_text },
        { "setclipboardtext", &set_clipboard_text },
        { "executeyield_internal", &os_executeyield_internal },
        { "issignaled",  &is_signaled },
        { "sleep",       &sleep },
        { "expandabbreviatedpath", &expand_abbreviated_path },
        { "isuseradmin", &is_user_admin },
        { "getfileversion", &get_file_version },
        // UNDOCUMENTED; internal use only.
        { "_globdirs",   &glob_dirs },  // Public os.globdirs method is in core.lua.
        { "_globfiles",  &glob_files }, // Public os.globfiles method is in core.lua.
        { "_makedirglobber", &make_dir_globber },
        { "_makefileglobber", &make_file_globber },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "os");

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pop(state, 1);
}
