// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <wchar.h>
#include <assert.h>

#include "hooks.h"
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/posixstat.h>
#include <readline/posixtime.h>

#define sizeof_array(x) (sizeof(x) / sizeof(x[0]))

//------------------------------------------------------------------------------
static wchar_t  fwrite_buf[2048];
void            (*rl_fwrite_function)(FILE*, const char*, int)  = NULL;
void            (*rl_fflush_function)(FILE*)                    = NULL;
extern int is_exec_ext(const char* ext);
extern void clear_suggestion();
extern void end_recognizer();
extern void end_task_manager();
extern void host_filter_transient_prompt(int crlf);
extern void terminal_begin_command();
extern int show_cursor(int visible);
extern int _rl_last_v_pos;

//------------------------------------------------------------------------------
static int mb_to_wide(const char* mb, wchar_t* fixed_wide, size_t fixed_size, wchar_t** out_wide, int* out_free)
{
    int needed = MultiByteToWideChar(CP_UTF8, 0, mb, -1, 0, 0);
    if (!needed)
        return 0;

    wchar_t* wide;
    int alloced;
    if ((size_t)needed <= fixed_size)
    {
        wide = fixed_wide;
        alloced = 0;
    }
    else
    {
        wide = (wchar_t*)malloc(needed * sizeof(*wide));
        if (!wide)
            return 0;
        alloced = !!wide;
    }

    int used = MultiByteToWideChar(CP_UTF8, 0, mb, -1, wide, needed);
    if (used != needed)
    {
        if (alloced)
            free(wide);
        return 0;
    }

    *out_wide = wide;
    *out_free = alloced;
    return used;
}

//------------------------------------------------------------------------------
int compare_string(const char* s1, const char* s2, int casefold)
{
    wchar_t tmp1[180];
    wchar_t tmp2[180];
    wchar_t* wide1;
    wchar_t* wide2;
    int free1 = 0;
    int free2 = 0;

    int cmp;
    if (mb_to_wide(s1, tmp1, sizeof_array(tmp1), &wide1, &free1) &&
        mb_to_wide(s2, tmp2, sizeof_array(tmp2), &wide2, &free2))
    {
        DWORD flags = SORT_DIGITSASNUMBERS|NORM_LINGUISTIC_CASING;
        if (casefold)
            flags |= LINGUISTIC_IGNORECASE;
        cmp = CompareStringW(LOCALE_USER_DEFAULT, flags, wide1, -1, wide2, -1);
        cmp -= CSTR_EQUAL;
    }
    else
    {
        assert(0);
        if (casefold)
            cmp = _stricmp(s1, s2);
        else
            cmp = strcmp(s1, s2);
    }

    if (free1)
        free(wide1);
    if (free2)
        free(wide2);

    return cmp;
}

//------------------------------------------------------------------------------
int hooked_fwrite(const void* data, int size, int count, FILE* stream)
{
    size *= count;

    if (rl_fwrite_function)
        rl_fwrite_function(stream, data, size);
    else
    {
        size_t characters;

        characters = MultiByteToWideChar(
            CP_UTF8, 0,
            (const char*)data, size,
            fwrite_buf, sizeof_array(fwrite_buf)
        );

        characters = characters ? characters : sizeof_array(fwrite_buf) - 1;
        fwrite_buf[characters] = L'\0';

        DWORD i;
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        WriteConsoleW(handle, fwrite_buf, (DWORD)wcslen(fwrite_buf), &i, NULL);
    }

    return size;
}

//------------------------------------------------------------------------------
int hooked_fprintf(FILE* stream, const char* format, ...)
{
    char buffer[2048];
    va_list v;

    va_start(v, format);
    vsnprintf(buffer, sizeof_array(buffer), format, v);
    va_end(v);

    buffer[sizeof_array(buffer) - 1] = '\0';
    hooked_fwrite(buffer, (int)strlen(buffer), 1, stream);
    return 0;
}

//------------------------------------------------------------------------------
int hooked_putc(int c, FILE* stream)
{
    char buf[2] = { (char)c, '\0' };
    hooked_fwrite(buf, 1, 1, stream);
    return 1;
}

//------------------------------------------------------------------------------
int hooked_fflush(FILE* stream)
{
    if (rl_fflush_function != NULL)
        (*rl_fflush_function)(stream);
    return 0;
}

//------------------------------------------------------------------------------
int hooked_fileno(FILE* stream)
{
    errno = EINVAL;
    return -1;
}

//------------------------------------------------------------------------------
int hooked_stat(const char* path, struct hooked_stat* out)
{
    memset(out, 0, sizeof(*out));

    // Utf8 to wchars.
    wchar_t buf[2048];
    size_t characters = MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof_array(buf));
    if (!characters)
    {
        errno = ENOENT;
        return -1;
    }
    buf[characters] = L'\0';

    // Get properties.
    struct _stat64 s;
    int ret = _wstat64(buf, &s);
    out->st_size = s.st_size;
    out->st_mode = s.st_mode;
    out->st_uid = s.st_uid;
    out->st_gid = s.st_gid;
    out->st_nlink = s.st_nlink;

    return ret;
}

//------------------------------------------------------------------------------
static int is_implied_dir(const wchar_t* path)
{
    if (!path)
        return 0;

    if (path[0] && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
        path += 2;

    if (!path[0])
        return 1;
    if ((path[0] == '/' || path[0] == '\\') && !path[1])
        return 1;

    return 0;
}

//------------------------------------------------------------------------------
// This implementation treats _S_IFLNK as a subvariant of _S_IFDIR and _S_IFREG.
// This makes it possible to tell the type (file or dir) without having to try
// to follow the symlink, and makes it possible to tell the type even when the
// symlink is orphaned.
int hooked_lstat(const char* path, struct hooked_stat* out)
{
    memset(out, 0, sizeof(*out));

    // Utf8 to wchars.
    wchar_t buf[2048];
    size_t characters = MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof_array(buf));
    if (!characters)
    {
error:
        errno = ENOENT;
        return -1;
    }
    buf[characters] = L'\0';

    // Get properties.
    int mode = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(buf, GetFileExInfoStandard, &fad))
        goto error;
    mode |= (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? _S_IFDIR|_S_IEXEC : _S_IFREG;
    mode |= (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? _S_IREAD : _S_IREAD|_S_IWRITE;
    if ((!S_ISDIR(mode) || is_implied_dir(buf)) && is_exec_ext(path))
        mode |= _S_IEXEC;
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        !(fad.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE))
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(buf, &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            if (fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK)
                mode |= _S_IFLNK;
            FindClose(h);
        }
    }
    mode |= (mode & 0700) >> 3;
    mode |= (mode & 0700) >> 6;

    out->st_mode = mode;
    out->st_size |= fad.nFileSizeLow;
    out->st_size |= ((unsigned __int64)fad.nFileSizeHigh) << 32;

    return 0;
}

//------------------------------------------------------------------------------
int hooked_fstat(int fid, struct hooked_stat* out)
{
    int ret;
    struct _stat64 s;

    ret = _fstat64(fid, &s);
    out->st_size = s.st_size;
    out->st_mode = s.st_mode;
    out->st_uid = s.st_uid;
    out->st_gid = s.st_gid;
    out->st_nlink = s.st_nlink;

    return ret;
}

//------------------------------------------------------------------------------
void end_prompt(int crlf)
{
    extern void end_prompt_lf();

    clear_suggestion();

    if (crlf < 0)
    {
        end_task_manager();
        end_recognizer();
    }

    host_filter_transient_prompt(crlf);

    _rl_move_vert(_rl_vis_botlin);
    if (crlf != 0)
        end_prompt_lf();
    if (crlf > 0)
        _rl_last_c_pos = 0;

    _rl_last_c_pos = 0;
    _rl_last_v_pos = 0;
    _rl_vis_botlin = 0;

    // Terminal shell integration.
    terminal_begin_command();
}

//------------------------------------------------------------------------------
void wait_for_input(unsigned long timeout)
{
    DWORD dummy;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h && GetConsoleMode(h, &dummy))
    {
        int was_visible = show_cursor(1);
        _rl_input_queued(timeout);
        if (!was_visible)
            show_cursor(0);
    }
}

//------------------------------------------------------------------------------
typedef unsigned long long uint64_t;
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    /* EPOCH is the number of 100 nanosecond intervals from
       January 1, 1601 (UTC) to January 1, 1970.
       (the correct value has 9 trailing zeros) */
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time =  ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
    return 0;
}

//------------------------------------------------------------------------------
static const char* s_trick_the_linker = 0;
void prevent_COMDAT_folding(const char* str)
{
    s_trick_the_linker = str;
}
