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

#include <rl/rl_line_editor.h>

//------------------------------------------------------------------------------
void                    load_history();
void                    save_history();
void                    shutdown_lua();
void                    shutdown_clink_settings();
int                     get_clink_setting_int(const char*);

inject_args_t           g_inject_args;
static line_editor_t*   g_line_editor           = NULL;
static const shell_t*   g_shell                 = NULL;
extern shell_t          g_shell_cmd;
#if 0
extern shell_t          g_shell_ps;
extern shell_t          g_shell_generic;
#endif

//------------------------------------------------------------------------------
static void initialise_line_editor()
{
    g_line_editor = initialise_rl_line_editor();
}

//------------------------------------------------------------------------------
static void shutdown_line_editor()
{
    if (g_line_editor != NULL)
        shutdown_rl_line_editor(g_line_editor);
}

//------------------------------------------------------------------------------
static void initialise_shell_name()
{
    char buffer[MAX_PATH];

    if (GetModuleFileName(NULL, buffer, sizeof_array(buffer)))
    {
        static char exe_name[64];
        const char* slash;
        
        slash = strrchr(buffer, '\\');
        slash = slash ? slash + 1 : buffer;

        str_cpy(exe_name, slash, sizeof(exe_name));
        set_shell_name(g_line_editor, exe_name);

        LOG_INFO("Setting shell name to '%s'", exe_name);
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
    // Get the inject arguments.
    get_inject_args(GetCurrentProcessId());

    // The "clink_profile" environment variable can be used to override --profile
    GetEnvironmentVariable("clink_profile", g_inject_args.profile_path,
        sizeof_array(g_inject_args.profile_path));

    if (g_inject_args.profile_path[0] != '\0')
    {
        set_config_dir_override(g_inject_args.profile_path);
    }
    if (g_inject_args.no_log)
    {
        disable_log();
    }

    // Prepare the process and environment for Readline.
    initialise_line_editor();
    initialise_shell_name();

    // Search for a supported shell.
    {
        int i;
        struct {
            const char*     name;
            const shell_t*  shell;
        } shells[] = {
            { "cmd.exe", &g_shell_cmd },
#if 0
            { "powershell.exe", &g_shell_ps },
#endif
        };

        for (i = 0; i < sizeof_array(shells); ++i)
        {
            const char* shell_name = get_shell_name(g_line_editor);
            if (stricmp(shell_name, shells[i].name) == 0)
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
            const char* shell_name = get_shell_name(g_line_editor);
            LOG_INFO("Unsupported shell '%s'", shell_name);
            return FALSE;
        }

#if 0
        g_shell = &g_shell_generic;
#endif
    }

    if (!g_shell->validate())
    {
        LOG_INFO("Shell validation failed.");
        return FALSE;
    }

    if (!g_shell->initialise(g_line_editor))
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
        shutdown_line_editor();
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
