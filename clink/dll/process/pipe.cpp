// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "pipe.h"

//------------------------------------------------------------------------------
int create_pipe(int flags, pipe_t* pipe)
{
    BOOL ok;
    SECURITY_ATTRIBUTES sa;

    // Init data.
    pipe->read = nullptr;
    pipe->write = nullptr;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = !!(flags & (ReadHandleInheritable|WriteHandleInheritable));

    // Create the pipe.
    ok = CreatePipe(&pipe->read, &pipe->write, &sa, 0);
    if (ok == FALSE)
    {
        return 0;
    }

    // Adjust inherit flags on the handles.
    if (!(flags & ReadHandleInheritable))
    {
        SetHandleInformation(pipe->read, HANDLE_FLAG_INHERIT, 0);
    }

    if (!(flags & WriteHandleInheritable))
    {
        SetHandleInformation(pipe->write, HANDLE_FLAG_INHERIT, 0);
    }

    return 1;
}

//------------------------------------------------------------------------------
void destroy_pipe(pipe_t* pipe)
{
    if (pipe->read)
    {
        CloseHandle(pipe->read);
        pipe->read = nullptr;
    }

    if (pipe->write)
    {
        CloseHandle(pipe->write);
        pipe->write = nullptr;
    }
}

//------------------------------------------------------------------------------
HANDLE duplicate_handle(HANDLE handle, DWORD target_pid)
{
    HANDLE out;
    HANDLE process;
    BOOL ok;

    process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, target_pid);
    if (process == nullptr)
    {
        return nullptr;
    }

    ok = DuplicateHandle(
        GetCurrentProcess(),
        handle,
        process,
        &out,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS
    );

    if (ok != TRUE)
    {
        out = nullptr;
    }

    CloseHandle(process);
    return out;
}

//------------------------------------------------------------------------------
void duplicate_pipe(pipe_t* out, const pipe_t* in, DWORD target_pid)
{
    out->read = duplicate_handle(in->read, target_pid);
    out->write = duplicate_handle(in->write, target_pid);
}
