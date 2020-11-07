// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <Windows.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <wchar.h>
#include <assert.h>

#include "hooks.h"

#define sizeof_array(x) (sizeof(x) / sizeof(x[0]))

//------------------------------------------------------------------------------
static wchar_t  fwrite_buf[2048];
void            (*rl_fwrite_function)(FILE*, const char*, int)  = NULL;
void            (*rl_fflush_function)(FILE*)                    = NULL;

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
        cmp = CompareStringW(LOCALE_USER_DEFAULT, SORT_DIGITSASNUMBERS, wide1, -1, wide2, -1);
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
void hooked_fprintf(FILE* stream, const char* format, ...)
{
    char buffer[2048];
    va_list v;

    va_start(v, format);
    vsnprintf(buffer, sizeof_array(buffer), format, v);
    va_end(v);

    buffer[sizeof_array(buffer) - 1] = '\0';
    hooked_fwrite(buffer, (int)strlen(buffer), 1, stream);
}

//------------------------------------------------------------------------------
int hooked_putc(int c, FILE* stream)
{
    char buf[2] = { (char)c, '\0' };
    hooked_fwrite(buf, 1, 1, stream);
    return 1;
}

//------------------------------------------------------------------------------
void hooked_fflush(FILE* stream)
{
    if (rl_fflush_function != NULL)
        (*rl_fflush_function)(stream);
}

//------------------------------------------------------------------------------
int hooked_fileno(FILE* stream)
{
    errno = EINVAL;
    return -1;
}

//------------------------------------------------------------------------------
#if 0 // Clink had forgotten to setlocale(LC_ALL, ".utf8").
size_t hooked_mbrtowc(wchar_t* out, const char* in, size_t size, mbstate_t* state)
{
#if 0
    return mbrtowc(out, in, size, state);
#else
    typedef struct {
        unsigned char   count;
        unsigned char   length;
        unsigned short  value;
    } inner_state_t;

    inner_state_t* inner_state;
    const char* read;

    if (in == NULL)
    {
        in = "";
        size = 1;
        out = NULL;
    }

    if (!*in)
        return 0;

    if (state == NULL)
    {
        if (out != NULL)
            *out = *in;
        return 1;
    }

    read = in;
    inner_state = (inner_state_t*)state;
    if (!inner_state->length)
    {
        if (*in & 0x80)
        {
            inner_state->length = 2;
            inner_state->length += (*read & 0x60) == 0x60;
            inner_state->length += !!(*read & 0x10);
            inner_state->value = *read & (0x1f >> inner_state->length);

            inner_state->count = 1;
            ++read;
            --size;
        }
        else
        {
            if (out != NULL)
                *out = *in;
            return 1;
        }
    }

    while (size && (inner_state->count < inner_state->length))
    {
        inner_state->value <<= 6;
        inner_state->value |= (*read & 0x7f);

        ++inner_state->count;
        ++read;
        --size;
    }

    if (inner_state->count == inner_state->length)
    {
        if (out != NULL)
            *out = inner_state->value;

        inner_state->length = 0;
        return (size_t)(read - in);
    }

    return -2;
#endif // 0
}
#endif

//------------------------------------------------------------------------------
#if 0 // Clink had forgotten to setlocale(LC_ALL, ".utf8").
size_t hooked_mbrlen(const char* in, size_t size, mbstate_t* state)
{
#if 0
    return mbrlen(in, size, state);
#else
    wchar_t t;
    return hooked_mbrtowc(&t, in, size, state);
#endif // 0
}
#endif

//------------------------------------------------------------------------------
int hooked_stat(const char* path, struct hooked_stat* out)
{
    int ret = -1;
    wchar_t buf[2048];
    size_t characters;

    // Utf8 to wchars.
    characters = MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, sizeof_array(buf));
    characters = characters ? characters : sizeof_array(buf) - 1;
    buf[characters] = L'\0';

    // Get properties.
#if 0
    out->st_size = 0;
    out->st_mode = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(buf, GetFileExInfoStandard, &fad) != 0)
    {
        unsigned dir_bit;

        dir_bit = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? _S_IFDIR : 0;

        out->st_size = fad.nFileSizeLow;
        out->st_mode |= dir_bit;
        ret = 0;
    }
    else
        errno = ENOENT;
#else
    struct _stat64 s;
    ret = _wstat64(buf, &s);
    out->st_size = s.st_size;
    out->st_mode = s.st_mode;
#endif

    return ret;
}

//------------------------------------------------------------------------------
int hooked_fstat(int fid, struct hooked_stat* out)
{
    int ret;
    struct _stat64 s;

    ret = _fstat64(fid, &s);
    out->st_size = s.st_size;
    out->st_mode = s.st_mode;

    return ret;
}
