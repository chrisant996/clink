/* Copyright (c) 2013 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "pipe.h"

//------------------------------------------------------------------------------
int create_pipe(int flags, pipe_t* pipe)
{
    BOOL ok;
    SECURITY_ATTRIBUTES sa;

    // Init data.
    pipe->read = NULL;
    pipe->write = NULL;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
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
        pipe->read = NULL;
    }

    if (pipe->write)
    {
        CloseHandle(pipe->write);
        pipe->write = NULL;
    }
}

//------------------------------------------------------------------------------
HANDLE duplicate_handle(HANDLE handle, DWORD target_pid)
{
    HANDLE out;
    HANDLE process;
    BOOL ok;

    process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, target_pid);
    if (process == NULL)
    {
        return NULL;
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
        out = NULL;
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
