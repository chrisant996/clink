/*

    Implementation of POSIX directory browsing functions and types for Win32.

    Author:  Kevlin Henney (kevlin@acm.org, kevlin@curbralan.com)
    History: Created March 1997. Updated June 2003.
    Rights:  See end of file.

*/

#include <Windows.h>

#include <dirent.h>
#include <errno.h>
#include <io.h> /* _wfindfirst and _wfindnext set errno iff they return -1 */
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

static const int MAX_NAME_LEN = 2048;

struct DIR
{
    long                handle; /* -1 for failed rewind */
    struct _wfinddata_t info;
    struct dirent       result; /* d_name null iff first time */
    wchar_t             *name;  /* null-terminated char string */
    char                *conv_buf;
};

DIR *opendir(const char *name)
{
    DIR *dir = 0;

    if (name && name[0])
    {
        size_t base_length = strlen(name);
        const wchar_t *all = /* search pattern must end with suitable wildcard */
            strchr("/\\", name[base_length - 1]) ? L"*" : L"/*";

        base_length += wcslen(all) + 1;
        base_length *= sizeof(wchar_t);

        if ((dir = (DIR *) malloc(sizeof *dir)) != 0 &&
           (dir->name = (wchar_t *) malloc(base_length)) != 0)
        {
            MultiByteToWideChar(
                CP_UTF8,
                0,
                name,
                -1,
                dir->name,
                (int)(base_length / sizeof(wchar_t))
            );
            wcscat(dir->name, all);

            if ((dir->handle = (long) _wfindfirst(dir->name, &dir->info)) != -1)
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

    if (dir && dir->handle != -1)
    {
        if (!dir->result.d_name || _wfindnext(dir->handle, &dir->info) != -1)
        {
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
        dir->handle = (long) _wfindfirst(dir->name, &dir->info);
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
