/*

    Implementation of POSIX directory browsing functions and types for Win32.

    Author:  Kevlin Henney (kevlin@acm.org, kevlin@curbralan.com)
    History: Created March 1997. Updated June 2003.
    Rights:  See end of file.

*/

#include <Windows.h>

#include <dirent.h>
#include <errno.h>
#include <io.h> /* _wfindfirst64 and _wfindnext64 set errno iff they return -1 */
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

extern int _rl_match_hidden_files;
static const int MAX_NAME_LEN = 2048;

struct DIR
{
    intptr_t              handle; /* -1 for failed rewind */
    struct _wfinddata64_t info;
    struct dirent         result; /* d_name null iff first time */
    wchar_t               *name;  /* null-terminated char string */
    char                  *conv_buf;
};

int is_volume_relative(const char* path)
{
    size_t i = strlen(path);
    if (i > 1)
    {
        if (path[1] == ':' && path[2] != '\\' && path[2] != '/')
        {
            return 1;
        }
    }

    return 0;
}

int get_volume_path(const char* path, wchar_t* buffer, int size)
{
    DWORD ev_size;
    wchar_t ev_name[4] = { L"=X:" };

    if (!is_volume_relative(path))
    {
        return -1;
    }

    ev_name[1] = (wchar_t)path[0];
    ev_size = GetEnvironmentVariableW(ev_name, buffer, size);
    if (size < (int)ev_size)
    {
        return ev_size;
    }

    if (ev_size == 0)
    {
        if (buffer && (size >= 4))
        {
            buffer[0] = ev_name[1];
            buffer[1] = L':';
            buffer[2] = L'/';
            buffer[3] = L'\0';
        }

        return 4;
    }

    if (size > (int)ev_size)
    {
        buffer[ev_size] = L'/';
        buffer[ev_size + 1] = L'\0';
    }

    return ev_size + 1;
}

DIR *opendir(const char *name)
{
    DIR *dir = 0;
    int offset = 0;
    int volume_relative = is_volume_relative(name);

    if (name && name[0])
    {
        size_t base_length = strlen(name);
        const wchar_t *all = L"";
        
        /* search pattern must end with suitable wildcard */
        if (!strchr(name, '*'))
        {
            all = strchr("/\\", name[base_length - 1]) ? L"*" : L"/*";
        }

        base_length += wcslen(all) + 1;
        if (volume_relative)
        {
            base_length += get_volume_path(name, NULL, 0);
        }
        base_length *= sizeof(wchar_t);

        if ((dir = (DIR *) malloc(sizeof *dir)) != 0 &&
           (dir->name = (wchar_t *) malloc(base_length)) != 0)
        {
            dir->name[0] = L'\0';

            if (volume_relative)
            {
                offset = get_volume_path(name, dir->name, (int)base_length);
                base_length -= offset;
                name += 2;
            }

            MultiByteToWideChar(
                CP_UTF8,
                0,
                name,
                -1,
                dir->name + offset,
                (int)(base_length / sizeof(wchar_t))
            );
            wcscat(dir->name, all);

            if ((dir->handle = (intptr_t) _wfindfirst64(dir->name, &dir->info)) != -1)
            {
                dir->conv_buf = (char*)malloc(MAX_NAME_LEN);
                dir->result.d_name = 0;
            }
            else /* rollback */
            {
                free(dir->name);
                free(dir);
                dir = 0;
            }
        }
        else /* rollback */
        {
            free(dir);
            dir   = 0;
            errno = ENOMEM;
        }
    }
    else
    {
        errno = EINVAL;
    }

    return dir;
}

int closedir(DIR *dir)
{
    int result = -1;

    if (dir)
    {
        if (dir->handle != -1)
        {
            result = _findclose(dir->handle);
        }

        free(dir->conv_buf);
        free(dir->name);
        free(dir);
    }

    if (result == -1) /* map all errors to EBADF */
    {
        errno = EBADF;
    }

    return result;
}

struct dirent *readdir(DIR *dir)
{
    struct dirent *result = 0;
    unsigned skip_mask;

    // Always skip system files, and maybe skip hidden ones.
    skip_mask = _A_SYSTEM;
    if (!_rl_match_hidden_files)
    {
        skip_mask |= _A_HIDDEN;
    }

    if (dir && dir->handle != -1)
    {
        while (!dir->result.d_name || _wfindnext64(dir->handle, &dir->info) != -1)
        {
            if (dir->info.attrib & skip_mask)
            {
                dir->result.d_name = "";
                continue;
            }

            WideCharToMultiByte(
                CP_UTF8,
                0,
                dir->info.name,
                -1,
                dir->conv_buf,
                MAX_NAME_LEN,
                NULL,
                NULL
            );

            result         = &dir->result;
            result->d_name = dir->conv_buf;
            result->attrib = dir->info.attrib;
            result->size   = dir->info.size;
            break;
        }
    }
    else
    {
        errno = EBADF;
    }

    return result;
}

void rewinddir(DIR *dir)
{
    if (dir && dir->handle != -1)
    {
        _findclose(dir->handle);
        dir->handle = (intptr_t) _wfindfirst64(dir->name, &dir->info);
        dir->result.d_name = 0;
    }
    else
    {
        errno = EBADF;
    }
}

#ifdef __cplusplus
}
#endif

/*

    Copyright Kevlin Henney, 1997, 2003. All rights reserved.

    Permission to use, copy, modify, and distribute this software and its
    documentation for any purpose is hereby granted without fee, provided
    that this copyright and permissions notice appear in all copies and
    derivatives.
    
    This software is supplied "as is" without express or implied warranty.

    But that said, if there are any problems please get in touch.

*/
