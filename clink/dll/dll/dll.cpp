// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "inject_args.h"
#include "paths.h"
#include "host/host.h"
#include "host/host_cmd.h"
#include "host/host_ps.h"

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

static bool             g_quiet         = false;
static line_editor*     g_line_editor   = nullptr;
static host*            g_host          = nullptr;

//------------------------------------------------------------------------------
static void initialise_line_editor(const char* host_name)
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

    line_editor::desc desc = { host_name, terminal, lua, printer };
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
static void success()
{
    if (!g_quiet)
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
static host* create_host_cmd(line_editor* editor)
{
    return new host_cmd(editor);
}

//------------------------------------------------------------------------------
static host* create_host_ps(line_editor* editor)
{
    return new host_ps(editor);
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
int initialise_clink(const inject_args* inject_args)
{
    // The "clink_profile" environment variable can be used to override --profile
    GetEnvironmentVariable("clink_profile", inject_args->profile_path,
        sizeof_array(inject_args->profile_path));

    // Handle inject arguments.
    if (inject_args->profile_path[0] != '\0')
        set_config_dir_override(inject_args->profile_path);

    if (!inject_args->no_log)
    {
        // Start a log file.
        str<256> log_path;
        get_log_dir(log_path);
        log_path << "/clink.log";
        new file_logger(log_path.c_str());
    }

    g_quiet = (inject_args->quiet != 0);

    // What process is the DLL loaded into?
    str<64> host_name;
    if (!get_host_name(host_name))
        return 0;

    LOG("Host process is '%s'", host_name.c_str());

    // Prepare core systems.
    initialise_clink_settings();
    initialise_line_editor(host_name.c_str());
    load_history();

    // Search for a supported host.
    struct {
        const char* name;
        host*      (*creator)(line_editor*);
    } hosts[] = {
        { "cmd.exe",        create_host_cmd },
        { "powershell.exe", create_host_ps },
    };

    for (int i = 0; i < sizeof_array(hosts); ++i)
    {
        if (stricmp(host_name.c_str(), hosts[i].name) == 0)
        {
            g_host = (hosts[i].creator)(g_line_editor);
            break;
        }
    }

    if (g_host == nullptr)
    {
        LOG("Unknown host.");
        return 0;
    }

    if (!g_host->validate())
    {
        LOG("Shell validation failed.");
        return 0;
    }

    if (!g_host->initialise())
    {
        failed();
        return 0;
    }

    success();
    return 1;
}

//------------------------------------------------------------------------------
void shutdown_clink()
{
    if (g_host == nullptr)
        return;

    g_host->shutdown();

    if (get_clink_setting_int("history_io"))
        load_history();

    save_history();
    shutdown_clink_settings();
    shutdown_line_editor();

    if (logger* logger = logger::get())
        delete logger;
}
