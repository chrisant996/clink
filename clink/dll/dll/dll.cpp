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
#include "paths.h"
#include "process/shared_mem.h"
#include "shell.h"
#include "shell_cmd.h"
#include "shell_ps.h"

#include <core/base.h>
#include <core/log.h>
#include <core/path.h>
#include <core/str.h>
#include <ecma48_terminal.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>
#include <matches/column_printer.h>
#include <rl/rl_line_editor.h>

//------------------------------------------------------------------------------
const char* g_clink_header =
    "Clink v"CLINK_VERSION" [git:"CLINK_COMMIT"] "
    "Copyright (c) 2012-2015 Martin Ridgers\n"
    "http://mridgers.github.io/clink\n"
    ;

//------------------------------------------------------------------------------
void*                   initialise_clink_settings();
void                    load_history();
void                    save_history();
void                    shutdown_clink_settings();
int                     get_clink_setting_int(const char*);

inject_args_t           g_inject_args;
static line_editor*     g_line_editor           = nullptr;
static shell*           g_shell                 = nullptr;

//------------------------------------------------------------------------------
static void initialise_line_editor(const char* shell_name)
{
    lua_root* lua = new lua_root();
    lua_State* state = lua->get_state();
    lua_load_script(state, dll, dir);
    lua_load_script(state, dll, env);
    lua_load_script(state, dll, exec);
    lua_load_script(state, dll, git);
    lua_load_script(state, dll, go);
    lua_load_script(state, dll, hg);
    lua_load_script(state, dll, p4);
    lua_load_script(state, dll, powershell);
    lua_load_script(state, dll, self);
    lua_load_script(state, dll, set);
    lua_load_script(state, dll, svn);

    terminal* terminal = new ecma48_terminal();
    match_printer* printer = new column_printer(terminal);

    line_editor::desc desc = { shell_name, terminal, lua, printer };
    g_line_editor = create_rl_line_editor(desc);
}

//------------------------------------------------------------------------------
static void shutdown_line_editor()
{
    terminal* term = g_line_editor->get_terminal();

    destroy_rl_line_editor(g_line_editor);
    delete term;
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
    if (!g_inject_args.quiet)
        puts(g_clink_header);
}

//------------------------------------------------------------------------------
static void failed()
{
    str<256> buffer;
    get_config_dir(buffer);

    fprintf(stderr, "Failed to load clink.\nSee log for details (%s).\n", buffer);
}

//------------------------------------------------------------------------------
static shell* create_shell_cmd(line_editor* editor)
{
    return new shell_cmd(editor);
}

//------------------------------------------------------------------------------
static shell* create_shell_ps(line_editor* editor)
{
    return new shell_ps(editor);
}

//------------------------------------------------------------------------------
static bool get_host_name(str_base& out)
{
    str<256> buffer;
    if (GetModuleFileName(nullptr, buffer.data(), buffer.size()) == buffer.size())
        return false;

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return false;

    return path::get_name(buffer.c_str(), out);
}

//------------------------------------------------------------------------------
void on_dll_attach()
{
    // First of all lets find out which process the DLL's loaded into.
    str<64> host_name;
    if (!get_host_name(host_name))
        return;

    // Start a log file.
    str<256> log_path;
    get_log_dir(log_path);
    log_path << "/clink.log";
    new file_logger(log_path.c_str());

    if (host_name.equals("clink_" AS_STR(PLATFORM) ".exe"))
        return;

    // Get the inject arguments.
    get_inject_args(GetCurrentProcessId());

    // The "clink_profile" environment variable can be used to override --profile
    GetEnvironmentVariable("clink_profile", g_inject_args.profile_path,
        sizeof_array(g_inject_args.profile_path));

    if (g_inject_args.profile_path[0] != '\0')
        set_config_dir_override(g_inject_args.profile_path);

    if (g_inject_args.no_log)
        delete logger::get();

    LOG("Host process is '%s'", host_name);

    // Prepare core systems.
    initialise_clink_settings();
    initialise_line_editor(host_name.c_str());
    load_history();

    // Search for a supported shell.
    struct {
        const char* name;
        shell*      (*creator)(line_editor*);
    } shells[] = {
        { "cmd.exe",        create_shell_cmd },
        { "powershell.exe", create_shell_ps },
    };

    for (int i = 0; i < sizeof_array(shells); ++i)
    {
        if (stricmp(host_name.c_str(), shells[i].name) == 0)
        {
            g_shell = (shells[i].creator)(g_line_editor);
            break;
        }
    }

    if (g_shell == nullptr)
    {
        LOG("Unknown shell.");
        return;
    }

    if (!g_shell->validate())
    {
        LOG("Shell validation failed.");
        return;
    }

    if (!g_shell->initialise())
    {
        failed();
        return;
    }

    success();
}

//------------------------------------------------------------------------------
void on_dll_detach()
{
    if (g_shell == nullptr)
        return;

    g_shell->shutdown();

    if (get_clink_setting_int("history_io"))
        load_history();

    save_history();
    shutdown_clink_settings();
    shutdown_line_editor();

    if (logger* logger = logger::get())
        delete logger;
}
