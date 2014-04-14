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
#include "shared/util.h"
#include "shared/pipe.h"

#if defined(__MINGW32__) && !defined(__MINGW64__)
#   define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 0x2000
#endif

//------------------------------------------------------------------------------
typedef struct
{
   PROCESS_INFORMATION  pi; 
   HANDLE               job;
   int                  timeout;
} exec_state_t;

//------------------------------------------------------------------------------
static HANDLE create_job()
{
    HANDLE handle;

    handle = CreateJobObject(NULL, NULL);
    if (handle != NULL)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_ex = { 0 };
        JOBOBJECT_BASIC_LIMIT_INFORMATION* limit = &limit_ex.BasicLimitInformation;

        limit->LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        if (SetInformationJobObject(handle, JobObjectExtendedLimitInformation,
            &limit_ex, sizeof(limit_ex)) == FALSE)
        {
            CloseHandle(handle);
            handle = NULL;
        }
    }

    return handle;
}

//------------------------------------------------------------------------------
static DWORD WINAPI thread_proc(exec_state_t* state)
{
    HANDLE process = state->pi.hProcess;
    HANDLE job = state->job;
    DWORD wait_result = WaitForSingleObject(process, state->timeout);

    CloseHandle(process);
    CloseHandle(job);
    return 0;
}

//------------------------------------------------------------------------------
static char* next_line(char* str)
{
    static const char* EOLS = "\r\n";
    char* eol;

    if (eol = strpbrk(str, EOLS))
    {
        do
        {
            *eol = '\0';
            ++eol;
        }
        while (*eol == '\r' || *eol == '\n');
    }
    else
    {
        eol = str + strlen(str);
    }

    return *eol ? eol : NULL;
}

//------------------------------------------------------------------------------
int lua_execute(lua_State* state)
{
    static const DWORD process_flags = NORMAL_PRIORITY_CLASS|CREATE_NO_WINDOW;

    const char* cmd;
    int arg_count;
    BOOL ok;
    STARTUPINFO si = { sizeof(si) };
    pipe_t pipe_stdout;
    pipe_t pipe_stderr;
    pipe_t pipe_stdin;
    exec_state_t exec_state;
    DWORD proc_ret;
    HANDLE thread;

    // Get the command line to execute.
    arg_count = lua_gettop(state);
    if (arg_count == 0 || !lua_isstring(state, 1))
    {
        return 0;
    }

    cmd = lua_tostring(state, 1);

    // Get the execution timeout.
    if (arg_count > 1 && lua_isnumber(state, 2))
    {
        exec_state.timeout = lua_tointeger(state, 2);
    }
    else
    {
        exec_state.timeout = 1000;
    }

    // Create a job object to manage the processes we'll spawn.
    exec_state.job = create_job(exec_state.timeout);
    if (exec_state.job == NULL)
    {
        return 0;
    }

    // Create pipes to redirect std* streams.
    create_pipe(WriteHandleInheritable, &pipe_stdout);
    create_pipe(WriteHandleInheritable, &pipe_stderr);
    create_pipe(ReadHandleInheritable, &pipe_stdin);

    // Launch the process.
    si.hStdError = pipe_stderr.write;
    si.hStdOutput = pipe_stdout.write;
    si.hStdInput = pipe_stdin.read;
    si.dwFlags = STARTF_USESTDHANDLES;

    ok = CreateProcess(NULL, (char*)cmd, NULL, NULL, TRUE, process_flags, NULL,
        NULL, &si, &exec_state.pi
    );
    if (ok == FALSE)
    {
        // Did it fail because the executable wasn't found? Maybe it's a batch
        // file? Best try running through the command processor.
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            char buffer[MAX_PATH];

            str_cpy(buffer, "cmd.exe /c ", sizeof_array(buffer));
            str_cat(buffer, cmd, sizeof_array(buffer));

            ok = CreateProcess(NULL, buffer, NULL, NULL, TRUE, process_flags,
                NULL, NULL, &si, &exec_state.pi
            );
        }

        if (ok == FALSE)
        {
            destroy_pipe(&pipe_stdout);
            destroy_pipe(&pipe_stderr);
            destroy_pipe(&pipe_stdin);
            CloseHandle(exec_state.job);
            return 0;
        }
    }

    AssignProcessToJobObject(exec_state.job, exec_state.pi.hProcess);
    thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_proc,
        &exec_state, 0, NULL
    );

    // Release our references to the child-side pipes. We don't use them, and
    // it means ReadFile() will leave the loop below once the child closes
    // the stdout pipe.
    CloseHandle(pipe_stdout.write);
    CloseHandle(pipe_stderr.write);
    CloseHandle(pipe_stdin.read);

    pipe_stdout.write = NULL;
    pipe_stderr.write = NULL;
    pipe_stdin.read = NULL;

    // Create a lua table.
    lua_newtable(state);

    // Read process' stdout, adding completed lines to Lua.
    {
        static const RESERVE = 4 * 1024 * 1024;

        void* buffer;
        char* write;
        int remaining;
        int page_size;
        SYSTEM_INFO sys_info;

        GetSystemInfo(&sys_info);
        page_size = sys_info.dwAllocationGranularity;
        buffer = VirtualAlloc(NULL, RESERVE, MEM_RESERVE, PAGE_READWRITE);

        write = (char*)buffer;
        remaining = 0;

        // Collect all the process's output.
        while (1)
        {
            DWORD bytes_read;
            BOOL ok;

            // Commit the next page if we're out of buffer space.
            if (remaining <= 1)
            {
                remaining = page_size;
                if (!VirtualAlloc(write, page_size, MEM_COMMIT, PAGE_READWRITE))
                {
                    break;
                }
            }

            // Read from the pipe ("- 1" to keep a null terminator around)
            ok = ReadFile(pipe_stdout.read, write, remaining - 1, &bytes_read, NULL);
            if (ok != TRUE)
            {
                break;
            }

            remaining -= bytes_read;
            write += bytes_read;
        }

        // Extract lines from the process's output.
        {
            int line_count = 0;
            char* line = (char*)buffer;
            char* next = NULL;

            do
            {
                next = next_line(line);

                lua_pushinteger(state, ++line_count);
                lua_pushstring(state, line);
                lua_rawset(state, -3);

                line = next;
            }
            while (next);
        }

        VirtualFree(buffer, 0, MEM_RELEASE);
    }

    proc_ret = -1;
    GetExitCodeProcess(exec_state.pi.hProcess, &proc_ret);
    lua_pushinteger(state, proc_ret);

    CloseHandle(exec_state.pi.hThread);

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    destroy_pipe(&pipe_stdout);
    destroy_pipe(&pipe_stderr);
    destroy_pipe(&pipe_stdin);

    return 2;
}

// vim: expandtab
