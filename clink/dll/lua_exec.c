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

//------------------------------------------------------------------------------
typedef struct 
{
    HANDLE  read;
    HANDLE  write;
} pipe_t;

typedef struct
{
   PROCESS_INFORMATION  pi; 
   HANDLE               job;
   int                  timeout;
} exec_state_t;

enum
{
    ReadHandleInheritable   = 1 << 0,
    WriteHandleInheritable  = 1 << 1,
};

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
    sa.bInheritHandle = TRUE;
    
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
    int proc_ret;
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

    ok = CreateProcess(NULL, cmd, NULL, NULL, TRUE, process_flags, NULL, NULL,
        &si, &exec_state.pi
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
    thread = CreateThread(NULL, 0, thread_proc, &exec_state, 0, NULL);

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
        static const BUF_SIZE = 1024;

        DWORD bytes_read;
        char* buffer;
        int line_count;

        line_count = 0;
        buffer = malloc(BUF_SIZE + 1);
        while (ReadFile(pipe_stdout.read, buffer, BUF_SIZE, &bytes_read, NULL) != FALSE)
        {
            char* line = buffer;
            char* next = NULL;

            buffer[bytes_read] = 0;
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
    }

    proc_ret = -1;
    GetExitCodeProcess(exec_state.pi.hProcess, &proc_ret);
    lua_pushinteger(proc_ret);

    CloseHandle(exec_state.pi.hThread);
    CloseHandle(thread);

    destroy_pipe(&pipe_stdout);
    destroy_pipe(&pipe_stderr);
    destroy_pipe(&pipe_stdin);

    return 2;
}
