// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "os.h"
#include "path.h"
#include "str.h"
#include "str_iter.h"
#include "str_compare.h"
#include <locale.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include <share.h>
#include <Shellapi.h>
#include <shlwapi.h>

#ifndef _MSC_VER
#define USE_PORTABLE
#endif

//------------------------------------------------------------------------------
#ifdef _MSC_VER
extern "C" void __cdecl __acrt_errno_map_os_error(unsigned long const oserrno);
#else
extern "C" void __cdecl __acrt_errno_map_os_error(unsigned long const oserrno)
{
    errno = EAGAIN;
}
#endif

//------------------------------------------------------------------------------
// Start the clock for os::clock() at global scope initialization.
static const os::high_resolution_clock s_clock;

//------------------------------------------------------------------------------
// We use UTF8 everywhere, and we need to tell the CRT so that mbrtowc and etc
// use UTF8 instead of the default CRT pseudo-locale.
class auto_set_locale_utf8
{
public:
    auto_set_locale_utf8() { setlocale(LC_ALL, ".utf8"); }
};
static auto_set_locale_utf8 s_auto_utf8;



//------------------------------------------------------------------------------
static class delay_load_mpr
{
public:
                        delay_load_mpr();
    bool                init();
    DWORD               WNetGetConnectionW(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[1];
        struct
        {
            DWORD (WINAPI* WNetGetConnectionW)(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength);
        };
    } m_procs;
} s_mpr;

//------------------------------------------------------------------------------
delay_load_mpr::delay_load_mpr()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_mpr::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        HMODULE hlib = LoadLibrary("mpr.dll");
        if (hlib)
            m_procs.proc[0] = GetProcAddress(hlib, "WNetGetConnectionW");
        m_ok = !!m_procs.WNetGetConnectionW;
    }

    return m_ok;
}

//------------------------------------------------------------------------------
DWORD delay_load_mpr::WNetGetConnectionW(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength)
{
    if (init() && !m_procs.WNetGetConnectionW)
        return ERROR_NOT_SUPPORTED;
    return m_procs.WNetGetConnectionW(lpLocalName, lpRemoteName, lpnLength);
}



//------------------------------------------------------------------------------
static class delay_load_shell32
{
public:
                        delay_load_shell32();
    bool                init();
    BOOL                IsUserAnAdmin();
    BOOL                ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[2];
        struct {
            BOOL (WINAPI* IsUserAnAdmin)();
            BOOL (WINAPI* ShellExecuteExW)(SHELLEXECUTEINFOW* pExecInfo);
        };
    } m_procs;
} s_shell32;

//------------------------------------------------------------------------------
delay_load_shell32::delay_load_shell32()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_shell32::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        HMODULE hlib = LoadLibrary("shell32.dll");
        if (hlib)
        {
            do
            {
                DLLGETVERSIONPROC pDllGetVersion;
                pDllGetVersion = DLLGETVERSIONPROC(GetProcAddress(hlib, "DllGetVersion"));
                if (!pDllGetVersion)
                    break;

                DLLVERSIONINFO dvi = { sizeof(dvi) };
                HRESULT hr = (*pDllGetVersion)(&dvi);
                if (FAILED(hr))
                    break;

                const DWORD dwVersion = MAKELONG(dvi.dwMinorVersion, dvi.dwMajorVersion);
                if (dwVersion < MAKELONG(0, 5))
                    break;

                m_procs.proc[0] = GetProcAddress(hlib, "IsUserAnAdmin");
                m_procs.proc[1] = GetProcAddress(hlib, "ShellExecuteExW");
            }
            while (false);
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
BOOL delay_load_shell32::IsUserAnAdmin()
{
    return init() && m_procs.IsUserAnAdmin();
}

//------------------------------------------------------------------------------
BOOL delay_load_shell32::ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo)
{
    return init() && m_procs.ShellExecuteExW(pExecInfo);
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
            len = GetEnvironmentVariableW(L"PATH", wpath.data(), wpath.size());
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



namespace os
{

//------------------------------------------------------------------------------
os::high_resolution_clock::high_resolution_clock()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    if (QueryPerformanceFrequency(&freq) &&
        QueryPerformanceCounter(&start) &&
        freq.QuadPart)
    {
        m_freq = double(freq.QuadPart);
        m_start = start.QuadPart;
    }
    else
    {
        m_freq = 0;
        m_start = 0;
    }
}

//------------------------------------------------------------------------------
double os::high_resolution_clock::elapsed() const
{
    if (!m_freq)
        return -1;

    LARGE_INTEGER current;
    if (!QueryPerformanceCounter(&current))
        return -1;

    const long long delta = current.QuadPart - m_start;
    if (delta < 0)
        return -1;

    const double result = double(delta) / m_freq;
    return result;
}

//------------------------------------------------------------------------------
cwd_restorer::cwd_restorer()
{
    GetCurrentDirectoryW(sizeof_array(m_path), m_path);
}

//------------------------------------------------------------------------------
cwd_restorer::~cwd_restorer()
{
    wchar_t path[288];
    DWORD dw = GetCurrentDirectoryW(sizeof_array(path), path);
    if (!dw || dw >= sizeof_array(path) || wcscmp(path, m_path) != 0)
        SetCurrentDirectoryW(m_path);
}

//------------------------------------------------------------------------------
void map_errno() { __acrt_errno_map_os_error(GetLastError()); }
void map_errno(unsigned long const oserrno) { __acrt_errno_map_os_error(oserrno); }

//------------------------------------------------------------------------------
static int s_errorlevel = 0;
void set_errorlevel(int errorlevel) { s_errorlevel = errorlevel; }
int get_errorlevel() { return s_errorlevel; }

//------------------------------------------------------------------------------
static const wchar_t* s_shell_name = L"cmd.exe";
void set_shellname(const wchar_t* shell_name) { s_shell_name = shell_name; }
const wchar_t* get_shellname() { return s_shell_name; }

//------------------------------------------------------------------------------
// Everyone knows about '*' and '?'.  But there are FIVE wildcard characters!
//
//  * (asterisk)
//      Matches zero or more characters.
//
//  ? (question mark)
//      Matches a single character.
//
//  DOS_STAR (less than)
//      Matches zero or more characters until encountering and matching the
//      final period in the name.
//
//  DOS_QM (greater than)
//      Matches any single character or, upon encountering a period or end
//      of name string, advances the expression to the end of the set of
//      contiguous DOS_QMs.
//
//  DOS_DOT (double quote)
//      Matches either a period or zero characters beyond the name string.
//
// The last three are for MS-DOS compatibility, and they follow the MS-DOS
// semantics.  For example, MS-DOS filenames can contain spaces, but not
// immediately before or after the dot.
//
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-_fsrtl_advanced_fcb_header-fsrtlisnameinexpression
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-_fsrtl_advanced_fcb_header-fsrtldoesnamecontainwildcards
//
// #define ANSI_DOS_STAR   ('<')
// #define ANSI_DOS_QM     ('>')
// #define ANSI_DOS_DOT    ('"')
// #define DOS_STAR        (L'<')
// #define DOS_QM          (L'>')
// #define DOS_DOT         (L'"')
static bool has_wildcard(const wchar_t* path)
{
    if (!path)
        return false;
    for (; *path; ++path)
    {
        if (*path == '*' ||
            *path == '?' ||
            *path == '<' ||
            *path == '>' ||
            *path == '"')
        {
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
DWORD get_file_attributes(const wchar_t* path, bool* symlink)
{
    // FindFirstFileW can handle cases that GetFileAttributesW can't (e.g. files
    // open exclusively, some hidden/system files in the system root directory).
    // But it can't handle a root directory, so if the incoming path ends with a
    // separator then use GetFileAttributesW instead.
    if (!*path ||
        path::is_separator(path[wcslen(path) - 1]) ||
        has_wildcard(path))
    {
        DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            map_errno();
            return INVALID_FILE_ATTRIBUTES;
        }
        if (symlink)
            *symlink = false;
        return attr;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        map_errno();
        return INVALID_FILE_ATTRIBUTES;
    }

    if (symlink)
    {
        *symlink = ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                    !(fd.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) &&
                    (fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK));
    }

    FindClose(h);
    return fd.dwFileAttributes;
}

//------------------------------------------------------------------------------
DWORD get_file_attributes(const char* path, bool* symlink)
{
    wstr<280> wpath(path);
    return get_file_attributes(wpath.c_str(), symlink);
}

//------------------------------------------------------------------------------
int get_path_type(const char* path)
{
    DWORD attr = get_file_attributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return path_type_invalid;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return path_type_dir;

    return path_type_file;
}

//------------------------------------------------------------------------------
int get_drive_type(const char* _path, unsigned int len)
{
    wstr<280> wpath;
    str_iter path(_path, len);
    to_utf16(wpath, path);
    UINT type = GetDriveTypeW(wpath.c_str());
    switch (type)
    {
    case DRIVE_NO_ROOT_DIR:     return drive_type_invalid;
    case DRIVE_REMOVABLE:       return drive_type_removable;
    case DRIVE_FIXED:           return drive_type_fixed;
    case DRIVE_REMOTE:          return drive_type_remote;
    case DRIVE_CDROM:           return drive_type_removable;
    case DRIVE_RAMDISK:         return drive_type_ramdisk;
    default:                    return drive_type_unknown;
    }
}

//------------------------------------------------------------------------------
bool is_hidden(const char* path)
{
    DWORD attr = get_file_attributes(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN));
}

//------------------------------------------------------------------------------
int get_file_size(const char* path)
{
    wstr<280> wpath(path);
    void* handle = CreateFileW(wpath.c_str(), 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        map_errno();
        return -1;
    }

    int ret = GetFileSize(handle, nullptr); // 2Gb max I suppose...
    if (ret == INVALID_FILE_SIZE)
        map_errno();
    CloseHandle(handle);
    return ret;
}

//------------------------------------------------------------------------------
void get_current_dir(str_base& out)
{
    wstr<280> wdir;
    GetCurrentDirectoryW(wdir.size(), wdir.data());
    out = wdir.c_str();
}

//------------------------------------------------------------------------------
bool set_current_dir(const char* dir)
{
    wstr<280> wdir(dir);
    if (SetCurrentDirectoryW(wdir.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool make_dir(const char* dir)
{
    int type = get_path_type(dir);
    if (type == path_type_dir)
        return true;

    str<> next(dir);

    if (path::to_parent(next, nullptr)  && !path::is_root(next.c_str()))
        if (!make_dir(next.c_str()))
            return false;

    if (*dir)
    {
        wstr<280> wdir(dir);
        if (CreateDirectoryW(wdir.c_str(), nullptr))
            return true;
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool remove_dir(const char* dir)
{
    wstr<280> wdir(dir);
    if (RemoveDirectoryW(wdir.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool unlink(const char* path)
{
    wstr<280> wpath(path);
    if (DeleteFileW(wpath.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool move(const char* src_path, const char* dest_path)
{
    wstr<280> wsrc_path(src_path);
    wstr<280> wdest_path(dest_path);
    if (MoveFileW(wsrc_path.c_str(), wdest_path.c_str()))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool copy(const char* src_path, const char* dest_path)
{
    wstr<280> wsrc_path(src_path);
    wstr<280> wdest_path(dest_path);
    if (CopyFileW(wsrc_path.c_str(), wdest_path.c_str(), FALSE))
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool get_temp_dir(str_base& out)
{
    wstr<280> wout;
    unsigned int size = GetTempPathW(wout.size(), wout.data());
    if (!size)
    {
error:
        map_errno();
        return false;
    }

    if (size >= wout.size())
    {
        wout.reserve(size);
        if (!GetTempPathW(wout.size(), wout.data()))
            goto error;
    }

    out = wout.c_str();
    return true;
}

//------------------------------------------------------------------------------
FILE* create_temp_file(str_base* out, const char* _prefix, const char* _ext, temp_file_mode _mode, const char* _path)
{
    if (out)
        out->clear();

    // Start with base path.
    str<> path(_path);
    if (!path.length() && !get_temp_dir(path))
        return nullptr;

    // Append up to 8 UTF32 characters from prefix.
    str<> prefix(_prefix);
    str_iter iter(prefix);
    for (int i = 8; i--; iter.next());
    prefix.truncate(static_cast<unsigned int>(iter.get_pointer() - prefix.c_str()));
    if (!prefix.length())
        prefix.copy("tmp");
    path::append(path, prefix.c_str());

    // Append process ID.
    prefix.format("_%X_", GetCurrentProcessId());
    path.concat(prefix.c_str());

    // Remember the base path and prefix length.
    wstr<> wpath(path.c_str());
    unsigned int base_len = wpath.length();

    // Open mode.
    str<16> mode("w+");
    int oflag = _O_CREAT| _O_RDWR|_O_EXCL|_O_SHORT_LIVED;
    if (_mode & os::binary) { oflag |= _O_BINARY; mode << "b"; }
    if (_mode & os::delete_on_close) oflag |= _O_TEMPORARY;

    // Create unique temp file, iterating if necessary.
    FILE* f = nullptr;
    errno_t err = EINVAL;
    wstr<> wunique;
    wstr<> wext(_ext ? _ext : "");
    srand(GetTickCount());
    unsigned unique = (rand() & 0xff) + ((rand() & 0xff) << 8);
    for (unsigned attempts = 0xffff + 1; attempts--;)
    {
        wunique.format(L"%04.4X", unique);
        wpath << wunique;
        wpath << wext;

        // Do a little dance to work around MinGW not supporting the "x" mode
        // flag in _wsfopen.
        int fd = _wsopen(wpath.c_str(), oflag, _SH_DENYNO, _S_IREAD|_S_IWRITE);
        if (fd != -1)
        {
            f = _fdopen(fd, mode.c_str());
            if (f)
                break;
            // If _wsopen succeeds but _fdopen fails, then something strange is
            // going on.  Just error out instead of potentially looping for a
            // long time in that case (this loop does up to 65536 attempts).
            _get_errno(&err);
            _close(fd);
            _set_errno(err);
            return nullptr;
        }

        _get_errno(&err);
        if (err == EINVAL || err == EMFILE)
            break;
        if (err == EACCES)
        {
            // Break out if there is no such file and yet access is denied.
            // There's no point doing 65535 retries.
            const DWORD attr = GetFileAttributesW(wpath.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES)
                return nullptr;
        }

        unique++;
        wpath.truncate(base_len);
    }

    if (!f)
    {
        map_errno(ERROR_NO_MORE_FILES);
        return nullptr;
    }

    if (out)
    {
        wstr_iter tmpi(wpath.c_str(), wpath.length());
        to_utf8(*out, tmpi);
    }

    return f;
}

//------------------------------------------------------------------------------
bool expand_env(const char* in, unsigned int in_len, str_base& out, int* point)
{
    bool expanded = false;

    out.clear();

    str_iter iter(in, in_len);
    while (iter.more())
    {
        const char* start = iter.get_pointer();
        while (iter.more() && iter.peek() != '%')
            iter.next();
        const char* end = iter.get_pointer();
        if (start < end)
            out.concat(start, int(end - start));

        if (iter.more())
        {
            start = iter.get_pointer();
            const int offset = int(start - in);
            assert(iter.peek() == '%');
            iter.next();

            const char* name = iter.get_pointer();
            while (iter.more() && iter.peek() != '%')
                iter.next();

            str<> var;
            var.concat(name, int(iter.get_pointer() - name));
            end = iter.get_pointer();

            if (iter.more() && iter.peek() == '%' && !var.empty())
            {
                iter.next();

                str<> value;
                if (!os::get_env(var.c_str(), value))
                {
                    end++;
                    goto LLiteral;
                }
                out << value.c_str();
                expanded = true;

                if (point && *point > offset)
                {
                    const int replaced_end = int(iter.get_pointer() - in);
                    if (*point <= replaced_end)
                        *point = offset + value.length();
                    else
                        *point += value.length() - (replaced_end - offset);
                }
            }
            else
            {
LLiteral:
                out.concat(start, int(end - start));
            }
        }
    }

    return expanded;
}

//------------------------------------------------------------------------------
bool get_env(const char* name, str_base& out)
{
    wstr<32> wname(name);

    int len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (!len)
    {
        if (stricmp(name, "HOME") == 0)
        {
            str<> a;
            str<> b;
            if (get_env("HOMEDRIVE", a) && get_env("HOMEPATH", b))
            {
                out.clear();
                out << a.c_str() << b.c_str();
                return true;
            }
            else if (get_env("USERPROFILE", out))
            {
                return true;
            }
        }
        else if (stricmp(name, "ERRORLEVEL") == 0)
        {
            out.clear();
            out.format("%d", os::get_errorlevel());
            return true;
        }
        else if (stricmp(name, "CD") == 0)
        {
            os::get_current_dir(out);
            return true;
        }
        else if (stricmp(name, "RANDOM") == 0)
        {
            out.clear();
            out.format("%d", rand());
            return true;
        }
        else if (stricmp(name, "CMDCMDLINE") == 0)
        {
            out = GetCommandLineW();
            return true;
        }

        map_errno();
        return false;
    }

    wstr<> wvalue;
    wvalue.reserve(len);
    len = GetEnvironmentVariableW(wname.c_str(), wvalue.data(), wvalue.size());

    out.reserve(len);
    out = wvalue.c_str();
    return true;
}

//------------------------------------------------------------------------------
bool set_env(const char* name, const char* value)
{
    wstr<32> wname(name);

    wstr<64> wvalue;
    if (value != nullptr)
        wvalue = value;

    // Update the host's environment via the C/C++ runtime so that Lua/etc are
    // affected.  Because the `set` command in CMD.EXE looks at a snapshot of
    // the environment, it won't see updates until it takes a new snapshot,
    // which happens whenever `set` sets or clears a variable.  But because
    // _wputenv_s does not support some things that SetEnvironmentVariableW,
    // this is just a secondary step to attempt to keep the C/C++ runtime in
    // sync with the real environment.
    _wputenv_s(wname.c_str(), wvalue.c_str());

    // Update the host's environment string table (CMD.EXE).  This is the
    // primary step, and lets Clink set variables named "=clink.profile", for
    // example.
    // NOTE:  This does not invoke the hooked version, and it does not intercept
    // setting PROMPT from inside Clink.
    const wchar_t* value_arg = (value != nullptr) ? wvalue.c_str() : nullptr;
    if (SetEnvironmentVariableW(wname.c_str(), value_arg) != 0)
        return true;

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
bool get_alias(const char* name, str_base& out)
{
    wstr<32> alias_name;
    alias_name = name;

    // Get the alias (aka. doskey macro).
    wstr<32> buffer;
    buffer.reserve(8191);
    if (GetConsoleAliasW(alias_name.data(), buffer.data(), buffer.size(), const_cast<wchar_t*>(s_shell_name)) == 0)
    {
        map_errno();
        return false;
    }

    if (!buffer.length())
    {
        errno = 0;
        return false;
    }

    out = buffer.c_str();
    return true;
}

//------------------------------------------------------------------------------
bool get_short_path_name(const char* path, str_base& out)
{
    wstr<> wpath(path);

    out.clear();

    unsigned int len = GetShortPathNameW(wpath.c_str(), nullptr, 0);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetShortPathNameW(wpath.c_str(), wout.data(), wout.size() - 1);
        if (len)
        {
            wstr_iter tmpi(wout.c_str(), wout.length());
            to_utf8(out, tmpi);
        }
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool get_long_path_name(const char* path, str_base& out)
{
    wstr<> wpath(path);

    out.clear();

    unsigned int len = GetLongPathNameW(wpath.c_str(), nullptr, 0);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetLongPathNameW(wpath.c_str(), wout.data(), wout.size() - 1);
        if (len)
        {
            wstr_iter tmpi(wout.c_str(), wout.length());
            to_utf8(out, tmpi);
        }
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool get_full_path_name(const char* _path, str_base& out, unsigned int len)
{
    wstr<> wpath;
    str_iter path(_path, len);
    to_utf16(wpath, path);

    out.clear();

    len = GetFullPathNameW(wpath.c_str(), 0, nullptr, nullptr);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetFullPathNameW(wpath.c_str(), wout.size() - 1, wout.data(), nullptr);
        if (len)
        {
            wstr_iter tmpi(wout.c_str(), wout.length());
            to_utf8(out, tmpi);
        }
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool get_net_connection_name(const char* path, str_base& out)
{
    errno = 0;

    WCHAR drive[4];
    drive[0] = path ? path[0] : '\0';
    if (drive[0])
        drive[1] = path[1];

    // Don't clear out until after using path, so the same string buffer can be
    // used as both input and output.
    out.clear();

    if (!drive[0])
        return true;

    drive[2] = '\\';
    drive[3] = '\0';
    if (GetDriveTypeW(drive) != DRIVE_REMOTE)
        return true;

    drive[2] = '\0';
    WCHAR remote[MAX_PATH];
    DWORD len = sizeof_array(remote);
    DWORD err = s_mpr.WNetGetConnectionW(drive, remote, &len);

    switch (err)
    {
    case NO_ERROR:
        to_utf8(out, remote);
        return true;
    case ERROR_NOT_CONNECTED:
    case ERROR_NOT_SUPPORTED:
        return true;
    }

    map_errno();
    return false;
}

//------------------------------------------------------------------------------
double clock()
{
    return s_clock.elapsed();
}

//------------------------------------------------------------------------------
time_t filetime_to_time_t(const FILETIME& ft)
{
    ULARGE_INTEGER uli;

    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    // Convert to time_t.
    uli.QuadPart -= 116444736000000000;
    uli.QuadPart /= 10000000;

    // Make sure it's between the Unix epoch and armageddon.
    if (uli.QuadPart > INT_MAX)
        return -1;

    // Return the converted time.
    return time_t(uli.QuadPart);
}

//------------------------------------------------------------------------------
bool get_clipboard_text(str_base& out)
{
    bool got = false;
    if (OpenClipboard(nullptr))
    {
        str<1024> utf8;
        HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
        if (clip_data)
        {
            to_utf8(out, (wchar_t*)clip_data);
            got = true;
        }

        CloseClipboard();
    }
    return got;
}

//------------------------------------------------------------------------------
bool set_clipboard_text(const char* text, int length)
{
    int size = 0;
    if (length)
    {
        size = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0) * sizeof(wchar_t);
        if (!size)
            return false;
    }
    size += sizeof(wchar_t);

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT, size);
    if (mem == nullptr)
        return false;

    if (length)
    {
        wchar_t* data = (wchar_t*)GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, text, length, data, size);
        GlobalUnlock(mem);
    }

    if (OpenClipboard(nullptr) == FALSE)
    {
        GlobalFree(mem);
        return false;
    }

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem); // Windows automatically dynamically converts to CF_TEXT as needed.
    CloseClipboard();
    return true;
}

//------------------------------------------------------------------------------
#if 0
void append_argv(str_base& out, const char* arg, argv_quote_mode mode)
{
    if (!(mode & argv_quote_mode::force) && *arg && !strpbrk(arg, " \t\r\n\v\""))
    {
        out.concat(arg);
        return;
    }

    if (mode & argv_quote_mode::for_cmd)
        out.concat("^", 1);
    out.concat("\"", 1);

    while (*arg)
    {
        unsigned num_backslash = 0;

        while (*arg == '\\')
        {
            num_backslash++;
            arg++;
        }

        if (!*arg)
        {
            while (num_backslash--)
                out.concat("\\\\", 2);
            break;
        }
        else if (*arg == '\"')
        {
            while (num_backslash--)
                out.concat("\\\\", 2);
            out.concat("\\", 1);
            if (mode & argv_quote_mode::for_cmd)
                out.concat("^", 1);
            out.concat("\"", 1);
        }
        else
        {
            while (num_backslash--)
                out.concat("\\", 1);
            if ((mode & argv_quote_mode::for_cmd) && strchr("()%!^\"<>&|", *arg))
                out.concat("^", 1);
            out.concat(arg, 1);
        }

        arg++;
    }

    if (mode & argv_quote_mode::for_cmd)
        out.concat("^", 1);
    out.concat("\"", 1);
}
#endif

//------------------------------------------------------------------------------
int system(const char* command, const char* cwd)
{
    int ret = -1;

    if (command)
    {
        HANDLE h = spawn_internal(command, cwd, nullptr, nullptr);
        if (h)
        {
            DWORD code;
            if (WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0 && GetExitCodeProcess(h, &code))
                ret = static_cast<int>(code);
            else
                map_errno();
            CloseHandle(h);
        }
    }

    return ret;
}

//------------------------------------------------------------------------------
HANDLE spawn_internal(const char* command, const char* cwd, HANDLE hStdin, HANDLE hStdout)
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

    const wchar_t* wcwd = nullptr;
    wstr<> wcwd_buffer;
    if (cwd)
    {
        wcwd_buffer = cwd;
        wcwd = wcwd_buffer.c_str();
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
        wcwd,
        &startup_info,
        &process_info);

    if (!child_status)
    {
        os::map_errno();
        return 0;
    }

    CloseHandle(process_info.hThread);
    return process_info.hProcess;
}

//------------------------------------------------------------------------------
HANDLE dup_handle(HANDLE process_handle, HANDLE h, bool inherit)
{
    HANDLE new_h = 0;
    if (!DuplicateHandle(process_handle,
                         h,
                         process_handle,
                         &new_h,
                         0,
                         inherit,
                         DUPLICATE_SAME_ACCESS))
    {
        os::map_errno();
        return 0;
    }
    return new_h;
}

//------------------------------------------------------------------------------
bool disambiguate_abbreviated_path(const char*& in, str_base& out)
{
    out.clear();

    // Strip quotes.  This may seem surprising, but it's what CMD does and it
    // works well.
    str_moveable tmp;
    concat_strip_quotes(tmp, in);

    // Find the last path separator.
    const char* last_sep = nullptr;
    for (const char* walk = tmp.c_str() + tmp.length(); walk-- > tmp.c_str();)
    {
        if (path::is_separator(*walk))
        {
            last_sep = walk;
            break;
        }
    }

    // If there are no path separators then there's nothing to disambiguate.
    if (!last_sep || last_sep == tmp.c_str())
        return false;

    // Don't operate on UNC paths, for performance reasons.
    if (path::is_unc(tmp.c_str()) || path::is_incomplete_unc(tmp.c_str()))
        return false;

    str<280> next;
    str<280> parse;
    wstr_moveable wnext;
    wstr_moveable wadd;

    // Any \\?\ segment should be kept as-is.
    const unsigned int ssqs = path::past_ssqs(tmp.c_str());
    parse.concat(tmp.c_str(), ssqs);

    // Don't operate on remote drives, for performance reasons.
    str<16> tmp2;
    if (path::get_drive(tmp.c_str() + ssqs, tmp2))
    {
        switch (os::get_drive_type(tmp2.c_str()))
        {
        case os::drive_type_invalid:
        case os::drive_type_remote:
            return false;
        }
    }
    parse << tmp2;

    // Identify the range to be parsed, up to but not including the last path
    // separator character.
    unsigned int parse_len = static_cast<unsigned int>(last_sep - tmp.c_str());
    wstr_moveable disambiguated;
    disambiguated = parse.c_str();

    bool unique = false;
    while (parse.length() < parse_len)
    {
        // Get next path component (e.g. "\dir").
        next.clear();
        while (parse.length() < parse_len)
        {
            const char ch = tmp.c_str()[parse.length()];
            if (!path::is_separator(ch))
                break;
            next.concat(&ch, 1);
            parse.concat(&ch, 1);
        }
        while (parse.length() < parse_len)
        {
            const char ch = tmp.c_str()[parse.length()];
            if (path::is_separator(ch))
                break;
            next.concat(&ch, 1);
            parse.concat(&ch, 1);
        }

        // Convert to UTF16.
        assert(next.length());
        wnext.clear();
        to_utf16(wnext, next.c_str());

        // Handle trailing path separators.
        assert(wnext.length());
        if (path::is_separator(wnext[wnext.length() - 1]))
        {
            const wchar_t ch = PATH_SEP_CHAR;
            disambiguated.concat(&ch, 1);
            assert(unique);
            break;
        }

        // Append star to check for ambiguous matches.
        const unsigned int committed = disambiguated.length();
        disambiguated << wnext << L"*";

        // Lookup in file system.
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(disambiguated.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return false;

        while (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (!FindNextFileW(h, &fd))
            {
                FindClose(h);
                return false;
            }
        }

        // Skip past the leading separator, if any, in the directory component.
        const wchar_t *dir = wnext.c_str();
        while (path::is_separator(*dir))
            ++dir;
        const unsigned int dir_len = static_cast<unsigned int>(wcslen(dir));

        // Copy file name because FindNextFileW will overwrite it.
        wadd = fd.cFileName;

        // Check for an exact or unique match.
        unique = wadd.iequals(dir);
        if (!unique)
        {
            wstr_moveable best;
            bool have_best = false;

            if (wcsncmp(dir, fd.cFileName, dir_len) == 0)
            {
                best = fd.cFileName;
                have_best = true;
            }

            do
            {
                unique = !FindNextFileW(h, &fd);
            }
            while (!unique && !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));

            if (!unique)
            {
                // Find lcd.
                int match_len = str_compare<wchar_t, true/*compute_lcd*/>(wadd.c_str(), fd.cFileName);
                if (match_len >= 0)
                    wadd.truncate(match_len);

                // Find best match for the case of the original input.
                do
                {
                    if (!have_best && wcsncmp(fd.cFileName, dir, dir_len) == 0)
                    {
                        best = fd.cFileName;
                        have_best = true;
                    }
                }
                while (FindNextFileW(h, &fd));
            }

            if (have_best)
            {
                int match_len = str_compare<wchar_t, true/*compute_lcd*/>(best.c_str(), wadd.c_str());
                if (match_len >= 0)
                    best.truncate(match_len);
                wadd = std::move(best);
            }
        }
        FindClose(h);

        // If lcd is empty, use the name from the input string (e.g. a leading
        // wildcard can cause this to happen).
        if (!wadd.length())
            wadd = dir;

        // Append the directory component to the disambiguated output string.
        disambiguated.truncate(committed);
        if (path::is_separator(wnext.c_str()[0]))
        {
            const wchar_t ch = PATH_SEP_CHAR;
            disambiguated.concat(&ch, 1);
        }
        disambiguated << wadd;

        // If it's not an exact or unique match, then it's been disambiguated
        // as much as possible.
        if (!unique)
        {
            // disambiguated contains the disambiguated part.
            // parsed contains the corresponding ambiguous part.

            // FUTURE: Use the rest of the directory components to do further
            // deductive disambiguation, instead of stopping?  Zsh even
            // restricts the completions accordingly...  But that seems
            // prohibitively complex in Clink due to how much control it
            // grants to match generators.

            break;
        }
    }

    // Return the disambiguated string.
    out.clear();
    to_utf8(out, disambiguated.c_str());

    // If the input is unambiguous and is already disambiguated then report that
    // disambiguation wasn't possible.
    if (unique && memcmp(out.c_str(), tmp.c_str(), out.length()) == 0)
    {
        out.clear();
        return false;
    }

    // Return how much of the input was disambiguated.
    in += parse.length();
    if (out.length() && path::is_separator(out[out.length() - 1]))
    {
        while (path::is_separator(*in))
            ++in;
    }

    // Return whether the input has been fully disambiguated.
    return unique;
}

//------------------------------------------------------------------------------
bool is_user_admin()
{
    return s_shell32.IsUserAnAdmin();
}

//------------------------------------------------------------------------------
bool run_as_admin(HWND hwnd, const wchar_t* file, const wchar_t* args)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.hwnd = hwnd;
    sei.fMask = SEE_MASK_FLAG_DDEWAIT|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = file;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;

    if (!s_shell32.ShellExecuteExW(&sei) || !sei.hProcess)
        return false;

    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exitcode = 999;
    if (!GetExitCodeProcess(sei.hProcess, &exitcode))
        exitcode = 1;
    CloseHandle(sei.hProcess);

    return exitcode == 0;
}

}; // namespace os



#if defined(DEBUG)

//------------------------------------------------------------------------------
int dbg_get_env_int(const char* name, int default_value)
{
    char tmp[32];
    int len = GetEnvironmentVariableA(name, tmp, sizeof(tmp));
    int val = (len > 0 && len < sizeof(tmp)) ? atoi(tmp) : default_value;
    return val;
}

//------------------------------------------------------------------------------
static void dbg_vprintf_row(int row, const char* fmt, va_list args)
{
    str<> tmp;
    if (row < 0)
    {
        tmp.vformat(fmt, args);
        wstr<> wtmp(tmp.c_str());
        OutputDebugStringW(wtmp.c_str());
        return;
    }

    tmp << "\x1b[s\x1b[";
    if (row > 0)
    {
        char buf[32];
        tmp << itoa(row, buf, 10);
    }
    tmp << "H" << fmt << "\x1b[K\x1b[u";
    vprintf(tmp.c_str(), args);
}

//------------------------------------------------------------------------------
void dbg_printf_row(int row, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    dbg_vprintf_row(row, fmt, args);

    va_end(args);
}

//------------------------------------------------------------------------------
void dbg_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    dbg_vprintf_row(0, fmt, args);

    va_end(args);
}

#endif
