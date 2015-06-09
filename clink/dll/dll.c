/* Copyright (c) 2012 Martin Ridgers
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
#include "inject_args.h"
#include "shell.h"
#include "shared/util.h"
#include "shared/shared_mem.h"

//------------------------------------------------------------------------------
void                    load_history();
void                    save_history();
void                    shutdown_lua();
void                    shutdown_clink_settings();
int                     get_clink_setting_int(const char*);
void                    prepare_env_for_inputrc();

inject_args_t           g_inject_args;
static const shell_t*   g_shell                 = NULL;
extern shell_t          g_shell_cmd;
extern shell_t          g_shell_ps;
extern shell_t          g_shell_generic;

//------------------------------------------------------------------------------
static void set_rl_readline_name()
{
    char buffer[MAX_PATH];

    if (GetModuleFileName(NULL, buffer, sizeof_array(buffer)))
    {
        static char exe_name[64];
        const char* slash;
        
        slash = strrchr(buffer, '\\');
        slash = slash ? slash + 1 : buffer;

        str_cpy(exe_name, slash, sizeof(exe_name));
        rl_readline_name = exe_name;

        LOG_INFO("Setting rl_readline_name to '%s'", exe_name);
    }
}

//------------------------------------------------------------------------------
static void get_inject_args(DWORD pid)
{
    shared_mem_t* shared_mem;
    shared_mem = open_shared_mem(1, "clink", pid);
    if (shared_mem)
    {
        memcpy(&g_inject_args, shared_mem->ptr, sizeof(g_inject_args));
        close_shared_mem(shared_mem);
    }
}

//------------------------------------------------------------------------------
static void success()
{
    extern const char* g_clink_header;

    if (!g_inject_args.quiet)
    {
        puts(g_clink_header);
    }
}

//------------------------------------------------------------------------------
static void failed()
{
    char buffer[1024];

    buffer[0] = '\0';
    get_config_dir(buffer, sizeof_array(buffer));

    fprintf(stderr, "Failed to load clink.\nSee log for details (%s).\n", buffer);
}

//------------------------------------------------------------------------------
static BOOL on_dll_attach()
{
    void* base;

    // Get the inject arguments.
    get_inject_args(GetCurrentProcessId());
    if (g_inject_args.profile_path[0] != '\0')
    {
        set_config_dir_override(g_inject_args.profile_path);
    }
    if (g_inject_args.no_log)
    {
        disable_log();
    }

    // Prepare the process and environment for Readline.
    set_rl_readline_name();
    prepare_env_for_inputrc();

    // Search for a supported shell.
    {
        int i;
        struct {
            const char*     name;
            const shell_t*  shell;
        } shells[] = {
            { "cmd.exe", &g_shell_cmd },
            { "powershell.exe", &g_shell_ps },
        };

        for (i = 0; i < sizeof_array(shells); ++i)
        {
            if (stricmp(rl_readline_name, shells[i].name) == 0)
            {
                g_shell = shells[i].shell;
                break;
            }
        }
    }

    // Not a supported shell?
    if (g_shell == NULL)
    {
        if (!g_inject_args.no_host_check)
        {
            LOG_INFO("Unsupported shell '%s'", rl_readline_name);
            return FALSE;
        }

        g_shell = &g_shell_generic;
    }

    if (!g_shell->validate())
    {
        LOG_INFO("Shell validation failed.");
        return FALSE;
    }

    base = GetModuleHandle(NULL);
    if (!g_shell->initialise(base))
    {
        failed();
        return FALSE;
    }

    success();
    return TRUE;
}

//------------------------------------------------------------------------------
static BOOL on_dll_detach()
{
    if (g_shell != NULL)
    {
        g_shell->shutdown();

        if (get_clink_setting_int("history_io"))
            load_history();

        save_history();
        shutdown_lua();
        shutdown_clink_settings();
    }

    return TRUE;
}

//------------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID unused)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:    return on_dll_attach();
    case DLL_PROCESS_DETACH:    return on_dll_detach();
    }

    return TRUE;
}

// vim: expandtab
