// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
typedef struct
{
    HANDLE  read;
    HANDLE  write;
} pipe_t;

enum
{
    ReadHandleInheritable   = 1 << 0,
    WriteHandleInheritable  = 1 << 1,
};

//------------------------------------------------------------------------------
int     create_pipe(int, pipe_t*);
void    destroy_pipe(pipe_t*);
HANDLE  duplicate_handle(HANDLE, DWORD);
void    duplicate_pipe(pipe_t*, const pipe_t*, DWORD);
