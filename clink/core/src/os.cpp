// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "os.h"
#include "cwd_restorer.h"
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
#include <shlobj.h>
#include <shlwapi.h>
#ifdef CAPTURE_PUSHD_STACK
#include <vector>
#endif

#ifndef _MSC_VER
#define USE_PORTABLE
#endif

//------------------------------------------------------------------------------
#if defined(__MINGW32__) || defined(__MINGW64__)
// {13709620-C279-11CE-A49E-444553540000}
static const GUID c_clsid_shell = { 0x13709620, 0xC279, 0x11CE, { 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
#define CLSID_Shell c_clsid_shell
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
    BOOL                ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[1];
        struct {
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

                m_procs.proc[0] = GetProcAddress(hlib, "ShellExecuteExW");
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
BOOL delay_load_shell32::ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo)
{
    return init() && m_procs.ShellExecuteExW(pExecInfo);
}



//------------------------------------------------------------------------------
static class delay_load_oleaut32
{
public:
                        delay_load_oleaut32();
    bool                init();
    HMODULE             module() const { return m_hlib; }
    void                VariantInit(VARIANTARG* pvarg);
    BSTR                SysAllocString(const OLECHAR* psz);
    void                SysFreeString(BSTR bstrString);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    HMODULE             m_hlib = 0;
    union
    {
        FARPROC         proc[3];
        struct {
            void        (WINAPI* VariantInit)(VARIANTARG* pvarg);
            BSTR        (WINAPI* SysAllocString)(const OLECHAR* psz);
            void        (WINAPI* SysFreeString)(BSTR bstrString);
        } proto;
    } m_procs;
} s_oleaut32;

//------------------------------------------------------------------------------
delay_load_oleaut32::delay_load_oleaut32()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_oleaut32::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        m_hlib = LoadLibrary("oleaut32.dll");
        if (m_hlib)
        {
            size_t c = 0;
            m_procs.proc[c++] = GetProcAddress(m_hlib, "VariantInit");
            m_procs.proc[c++] = GetProcAddress(m_hlib, "SysAllocString");
            m_procs.proc[c++] = GetProcAddress(m_hlib, "SysFreeString");
            assert(_countof(m_procs.proc) == c);
        }

        m_ok = true;
        static_assert(sizeof(m_procs.proc) == sizeof(m_procs), "proc[] dimension is too small");
        for (auto const& proc : m_procs.proc)
        {
            if (!proc)
            {
                m_ok = false;
                break;
            }
        }
    }

assert(m_ok);
    return m_ok;
}

//------------------------------------------------------------------------------
void delay_load_oleaut32::VariantInit(VARIANTARG* pvarg)
{
    if (!init())
    {
        ZeroMemory(pvarg, sizeof(*pvarg));
        return;
    }
    return m_procs.proto.VariantInit(pvarg);
}

BSTR delay_load_oleaut32::SysAllocString(const OLECHAR* psz)
{
    if (!init())
        return NULL;
    return m_procs.proto.SysAllocString(psz);
}

void delay_load_oleaut32::SysFreeString(BSTR bstrString)
{
    if (!init())
        return;
    return m_procs.proto.SysFreeString(bstrString);
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
        int32 len = GetEnvironmentVariableW(L"PATH", nullptr, 0);
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
        uint32 len = buffer.length();
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

    const int64 delta = current.QuadPart - m_start;
    if (delta < 0)
        return -1;

    const double result = double(delta) / m_freq;
    return result;
}

//------------------------------------------------------------------------------
cwd_restorer::cwd_restorer()
{
    os::get_current_dir(m_path);
}

//------------------------------------------------------------------------------
cwd_restorer::~cwd_restorer()
{
    str<> path;
    path.reserve(m_path.size(), true);
    os::get_current_dir(path);

    if (!path.equals(m_path.c_str()))
        os::set_current_dir(m_path.c_str());
}

//------------------------------------------------------------------------------
static clipboard_provider* s_clipboard_provider = nullptr;
void set_clipboard_provider(clipboard_provider* clip)
{
    s_clipboard_provider = clip;
}

//------------------------------------------------------------------------------
void map_errno() { __acrt_errno_map_os_error(GetLastError()); }
void map_errno(unsigned long const oserrno) { __acrt_errno_map_os_error(oserrno); }

//------------------------------------------------------------------------------
static int32 s_errorlevel = 0;
void set_errorlevel(int32 errorlevel) { s_errorlevel = errorlevel; }
int32 get_errorlevel() { return s_errorlevel; }

//------------------------------------------------------------------------------
#ifdef CAPTURE_PUSHD_STACK
static std::vector<str_moveable> s_pushd_stack;
void set_pushd_stack(std::vector<str_moveable>& stack) { s_pushd_stack.swap(stack); stack.clear(); }
void get_pushd_stack(std::vector<str_moveable>& stack) { stack.clear(); for (const auto& dir : s_pushd_stack) stack.emplace_back(dir.c_str()); }
int32 get_pushd_depth() { return int32(s_pushd_stack.size()); }
#else
static int32 s_pushd_depth = -1;
void set_pushd_depth(int32 depth) { s_pushd_depth = depth; }
int32 get_pushd_depth() { return s_pushd_depth; }
#endif

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
int32 get_path_type(const char* path)
{
    DWORD attr = get_file_attributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return path_type_invalid;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return path_type_dir;

    return path_type_file;
}

//------------------------------------------------------------------------------
int32 get_drive_type(const char* _path, uint32 len)
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
int32 get_file_size(const char* path)
{
    wstr<280> wpath(path);
    void* handle = CreateFileW(wpath.c_str(), 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        map_errno();
        return -1;
    }

    int32 ret = GetFileSize(handle, nullptr); // 2Gb max I suppose...
    if (ret == INVALID_FILE_SIZE)
        map_errno();
    CloseHandle(handle);
    return ret;
}

//------------------------------------------------------------------------------
bool get_current_dir(str_base& out)
{
    wstr<> wdir;

    const DWORD dwNeeded = GetCurrentDirectoryW(0, nullptr);
    if (!dwNeeded)
    {
error:
        map_errno();
clear_out:
        out.clear();
        return false;
    }

    if (!wdir.reserve(dwNeeded, true))
    {
nomem:
        map_errno(ENOMEM);
        goto clear_out;
    }

    const DWORD dwUsed = GetCurrentDirectoryW(wdir.size(), wdir.data());
    if (!dwUsed)
        goto error;
    if (dwUsed >= dwNeeded)
        goto nomem;

    out = wdir.c_str();
    return true;
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
    int32 type = get_path_type(dir);
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
    uint32 size = GetTempPathW(wout.size(), wout.data());
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
    for (int32 i = 8; i--; iter.next());
    prefix.truncate(uint32(iter.get_pointer() - prefix.c_str()));
    if (!prefix.length())
        prefix.copy("tmp");
    path::append(path, prefix.c_str());

    // Append process ID.
    prefix.format("_%X_", GetCurrentProcessId());
    path.concat(prefix.c_str());

    // Remember the base path and prefix length.
    wstr<> wpath(path.c_str());
    uint32 base_len = wpath.length();

    // Open mode.
    str<16> mode("w+");
    int32 oflag = _O_CREAT| _O_RDWR|_O_EXCL|_O_SHORT_LIVED;
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
        int32 fd = _wsopen(wpath.c_str(), oflag, _SH_DENYNO, _S_IREAD|_S_IWRITE);
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
        to_utf8(*out, wpath.c_str(), wpath.length());

    return f;
}

//------------------------------------------------------------------------------
bool expand_env(const char* in, uint32 in_len, str_base& out, int32* point)
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
            out.concat(start, int32(end - start));

        if (iter.more())
        {
            start = iter.get_pointer();
            const int32 offset = int32(start - in);
            assert(iter.peek() == '%');
            iter.next();

            const char* name = iter.get_pointer();
            while (iter.more() && iter.peek() != '%')
                iter.next();

            str<> var;
            var.concat(name, int32(iter.get_pointer() - name));
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
                    const int32 replaced_end = int32(iter.get_pointer() - in);
                    if (*point <= replaced_end)
                        *point = offset + value.length();
                    else
                        *point += value.length() - (replaced_end - offset);
                }
            }
            else
            {
LLiteral:
                out.concat(start, int32(end - start));
            }
        }
    }

    return expanded;
}

//------------------------------------------------------------------------------
bool get_env(const char* name, str_base& out)
{
    wstr<32> wname(name);

    int32 len = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
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
bool set_alias(const char* name, const char* command)
{
    if (!name || !command)
    {
        errno = EINVAL;
    }
    else
    {
        wstr<32> wname(name);
        wstr<32> wcommand(command);
        if (AddConsoleAliasW(wname.data(), wcommand.data(), const_cast<wchar_t*>(s_shell_name)))
            return true;
        map_errno();
    }
    return false;
}

//------------------------------------------------------------------------------
bool get_short_path_name(const char* path, str_base& out)
{
    wstr<> wpath(path);

    out.clear();

    uint32 len = GetShortPathNameW(wpath.c_str(), nullptr, 0);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetShortPathNameW(wpath.c_str(), wout.data(), wout.size() - 1);
        if (len)
            to_utf8(out, wout.c_str(), wout.length());
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

    uint32 len = GetLongPathNameW(wpath.c_str(), nullptr, 0);
    if (len)
    {
        wstr<> wout;
        wout.reserve(len);
        len = GetLongPathNameW(wpath.c_str(), wout.data(), wout.size() - 1);
        if (len)
            to_utf8(out, wout.c_str(), wout.length());
    }

    if (!len)
    {
        map_errno();
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
bool get_full_path_name(const char* _path, str_base& out, uint32 len)
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
            to_utf8(out, wout.c_str(), wout.length());
    }

    if (!len)
    {
        const DWORD last_err = GetLastError();
        map_errno();
        assert(last_err == GetLastError());
        SetLastError(last_err);
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
    if (s_clipboard_provider)
        return s_clipboard_provider->get_clipboard_text(out);

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
bool set_clipboard_text(const char* text, int32 length)
{
    if (s_clipboard_provider)
        return s_clipboard_provider->set_clipboard_text(text, length);

    int32 cch = 0;
    if (length)
    {
        cch = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0);
        if (!cch)
            return false;
    }
    ++cch;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT, cch * sizeof(wchar_t));
    if (mem == nullptr)
        return false;

    if (length)
    {
        wchar_t* data = (wchar_t*)GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, text, length, data, cch);
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
int32 system(const char* command, const char* cwd)
{
    int32 ret = -1;

    if (command)
    {
        HANDLE h = spawn_internal(command, cwd);
        if (h)
        {
            DWORD code;
            if (WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0 && GetExitCodeProcess(h, &code))
                ret = int32(code);
            else
                map_errno();
            CloseHandle(h);
        }
    }

    return ret;
}

//------------------------------------------------------------------------------
HANDLE spawn_internal(const char* command, const char* cwd, HANDLE hStdin, HANDLE hStdout, bool create_no_window)
{
    // Determine which command processor to use:  command.com or cmd.exe:
    static wchar_t const default_cmd_exe[] = L"cmd.exe";
    wstr_moveable comspec;
    const wchar_t* cmd_exe = default_cmd_exe;
    {
        int32 len = GetEnvironmentVariableW(L"COMSPEC", nullptr, 0);
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
        create_no_window ? CREATE_NO_WINDOW : 0,
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
HRESULT shell_unzip(const char* zip, const char* dest, str_base& err_msg)
{
    err_msg.clear();

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
final_ret:
        return hr;
    }

    Folder* pZipFolder = nullptr;
    Folder* pDestFolder = nullptr;
    FolderItems* pItems = nullptr;
    VARIANT vZip, vDest, vOpt, vItems;
    s_oleaut32.VariantInit(&vZip);
    s_oleaut32.VariantInit(&vDest);
    s_oleaut32.VariantInit(&vOpt);
    s_oleaut32.VariantInit(&vItems);

    // Create Shell.Application COM object.
    IShellDispatch* pShell = nullptr;
    hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pShell));
    if (FAILED(hr))
    {
uninit_all:
        if (FAILED(hr))
        {
            str<> tmp;
            format_error_message(hr, tmp);
            tmp.trim();
            if (tmp.length() && !ispunct(uint8(tmp.c_str()[tmp.length() - 1])))
                tmp.concat(".", 1);
            err_msg.trim();
            err_msg.format("\n%s", tmp.c_str());
        }
        if (vZip.vt == VT_BSTR) s_oleaut32.SysFreeString(vZip.bstrVal);
        if (vDest.vt == VT_BSTR) s_oleaut32.SysFreeString(vDest.bstrVal);
        if (pItems) pItems->Release();
        if (pZipFolder) pZipFolder->Release();
        if (pDestFolder) pDestFolder->Release();
        if (pShell) pShell->Release();
        CoUninitialize();
        goto final_ret;
    }

    str_moveable zip_full;
    if (!get_full_path_name(zip, zip_full))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        err_msg.format("Unable to get full path name of '%s'.", zip);
        goto uninit_all;
    }

    wstr_moveable wzip_full(zip_full.c_str());
    vZip.vt = VT_BSTR;
    vZip.bstrVal = s_oleaut32.SysAllocString(wzip_full.c_str());
    hr = pShell->NameSpace(vZip, &pZipFolder);
    if (FAILED(hr))
    {
        err_msg.format("Unable to access zip file '%s'.", zip);
        goto uninit_all;
    }
    if (!pZipFolder)
    {
        hr = E_INVALIDARG;
        err_msg.format("Invalid argument '%s'.", zip);
        goto uninit_all;
    }

    str_moveable dest_full;
    if (!get_full_path_name(dest, dest_full))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        err_msg.format("Unable to get full path name of '%s'.", dest);
        goto uninit_all;
    }

    wstr_moveable wdest_full(dest_full.c_str());
    vDest.vt = VT_BSTR;
    vDest.bstrVal = s_oleaut32.SysAllocString(wdest_full.c_str());
    if (FAILED(hr))
        goto uninit_all;

    hr = pShell->NameSpace(vDest, &pDestFolder);
    if (FAILED(hr))
    {
        err_msg.format("Unable to access destination folder '%s'.", dest);
        goto uninit_all;
    }
    if (!pDestFolder)
    {
        hr = E_INVALIDARG;
        err_msg.format("Invalid argument '%s'.", dest);
        goto uninit_all;
    }

    hr = pZipFolder->Items(&pItems);
    if (FAILED(hr))
    {
no_items_error:
        err_msg.format("Unable to get item list from zip file '%s'.", zip);
        goto uninit_all;
    }
    if (!pItems)
    {
        hr = E_POINTER;
        goto no_items_error;
    }

    vItems.vt = VT_DISPATCH;
    vItems.pdispVal = pItems;
    vOpt.vt = VT_I4;
    vOpt.lVal = FOF_NO_UI;
    hr = pDestFolder->CopyHere(vItems, vOpt);
    if (FAILED(hr))
    {
        err_msg.format("Unable to extract files from '%s' to '%s'.", zip, dest);
        goto uninit_all;
    }

    hr = S_OK;
    goto uninit_all;
}

//------------------------------------------------------------------------------
bool format_error_message(DWORD code, str_base& out, const char* tag, const char* sep)
{
    out.clear();

    wchar_t buf[1024];
    const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD cch = FormatMessageW(flags, 0, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof_array(buf), nullptr);

    if (!tag)
        tag = "Error";

    if (code < 65536)
        out.format("%s %u", tag, code);
    else
        out.format("%s 0x%08X", tag, code);

    bool need_term = true;
    if (cch > 0 && cch < sizeof_array(buf))
    {
        str_moveable tmp;
        if (to_utf8(tmp, buf))
        {
            need_term = false;
            out.concat(sep ? sep : ".  ");
            out.concat(tmp.c_str());
            out.trim();
        }
    }

    if (need_term && !sep)
        out.concat(".");

    return !out.empty();
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
void make_version_string(str_base& out)
{
    out.clear();

#pragma warning(push)
#pragma warning(disable:4996)
    OSVERSIONINFO ver = { sizeof(ver) };
    if (!GetVersionEx(&ver))
        return;
#pragma warning(pop)

    out.format("%d.%d.%05d", ver.dwMajorVersion, ver.dwMinorVersion, ver.dwBuildNumber);

    HKEY hkey = 0;
    if (!RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion", 0, MAXIMUM_ALLOWED, &hkey))
    {
        DWORD type = REG_DWORD;
        DWORD revision = 0;
        DWORD len = sizeof(revision);
        if (!RegQueryValueEx(hkey, "UBR", 0, &type, (LPBYTE)&revision, &len) &&
            type == REG_DWORD &&
            len == sizeof(revision))
        {
            str<> tmp;
            tmp.format(".%d", revision);
            out.concat(tmp.c_str());
        }
        RegCloseKey(hkey);
    }
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
    const uint32 ssqs = path::past_ssqs(tmp.c_str());
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

    // Don't operate on just a drive.
    const uint32 parse_len = uint32(last_sep - tmp.c_str());
    if (parse.length() >= parse_len)
    {
        out.clear();
        return false;
    }

    // Identify the range to be parsed, up to but not including the last path
    // separator character.
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
            const wchar_t ch = wnext[wnext.length() - 1];
            disambiguated.concat(&ch, 1);
            assert(unique);
            break;
        }

        // Append star to check for ambiguous matches.
        const uint32 committed = disambiguated.length();
        disambiguated << wnext << L"*";

        // Lookup in file system.
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(disambiguated.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return false;

        // Skip "." and/or ".." if they weren't explicitly requested.
        if (wcscmp(fd.cFileName, L".") == 0)
        {
            if (!disambiguated.equals(L"."))
            {
                if (!FindNextFileW(h, &fd))
                {
close_bail:
                    FindClose(h);
                    return false;
                }
            }
            if (!disambiguated.equals(L".."))
            {
                if (!FindNextFileW(h, &fd))
                    goto close_bail;
            }
        }

        while (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (!FindNextFileW(h, &fd))
                goto close_bail;
        }

        // Skip past the leading separator, if any, in the directory component.
        const wchar_t *dir = wnext.c_str();
        while (path::is_separator(*dir))
            ++dir;
        const uint32 dir_len = uint32(wcslen(dir));

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
                int32 match_len = str_compare<wchar_t, true/*compute_lcd*/>(wadd.c_str(), fd.cFileName);
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
                int32 match_len = str_compare<wchar_t, true/*compute_lcd*/>(best.c_str(), wadd.c_str());
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
            const wchar_t ch = wnext.c_str()[0];
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
bool is_elevated()
{
    bool elevated = false;

    HANDLE token = 0;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, false, &token) ||
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        DWORD size = 0;
        TOKEN_ELEVATION_TYPE elevation_type = TokenElevationTypeDefault;
        if (GetTokenInformation(token, TokenElevationType, &elevation_type, sizeof(elevation_type), &size))
        {
            switch (elevation_type)
            {
            case TokenElevationTypeFull:
                elevated = true;
                break;
            case TokenElevationTypeDefault:
                {
                    TOKEN_ELEVATION elevation = {};
                    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
                        elevated = !!elevation.TokenIsElevated;
                }
                break;
            }
        }
        CloseHandle(token);
    }

    return elevated;
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



//------------------------------------------------------------------------------
// Make the clock available to C code (e.g. in Readline).
extern "C" double os_clock(void)
{
    return os::clock();
}



#if defined(DEBUG)

//------------------------------------------------------------------------------
int32 dbg_get_env_int(const char* name, int32 default_value)
{
    char tmp[32];
    int32 len = GetEnvironmentVariableA(name, tmp, sizeof(tmp));
    int32 val = (len > 0 && len < sizeof(tmp)) ? atoi(tmp) : default_value;
    return val;
}

//------------------------------------------------------------------------------
static void dbg_vprintf_row(int32 row, const char* fmt, va_list args)
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
void dbg_printf_row(int32 row, const char* fmt, ...)
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
