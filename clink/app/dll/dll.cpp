// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_cmd.h"
#include "host/host_ps.h"
#include "inject_args.h"
#include "paths.h"
#include "rl/rl_line_editor.h"
#include "seh_scope.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <file_match_generator.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_root.h>
#include <lua/lua_script_loader.h>
#include <matches/column_printer.h>
#include <terminal/ecma48_terminal.h>

extern "C" {
#include <lauxlib.h>
}

//------------------------------------------------------------------------------
const char* g_clink_header =
    "Clink v"CLINK_VERSION" [git:"CLINK_COMMIT"] "
    "Copyright (c) 2012-2016 Martin Ridgers\n"
    "http://mridgers.github.io/clink\n"
    ;

//------------------------------------------------------------------------------
void*                   initialise_clink_settings();
void                    shutdown_clink_settings();
void                    load_history();
void                    save_history();
int                     get_clink_setting_int(const char*);
const char*             get_clink_setting_str(const char*);

static bool             g_quiet         = false;
static line_editor*     g_line_editor   = nullptr;
static host*            g_host          = nullptr;
static lua_root*        g_lua           = nullptr;

//------------------------------------------------------------------------------
static void load_lua_script(lua_State* lua, const char* path)
{
    str<> buffer;
    path::join(path, "*.lua", buffer);

    globber lua_globs(buffer.c_str());
    lua_globs.directories(false);

    while (lua_globs.next(buffer))
    {
        if (luaL_dofile(lua, buffer.c_str()) == 0)
            continue;

        if (const char* error = lua_tostring(lua, -1))
            puts(error);
    }
}

//------------------------------------------------------------------------------
static void load_lua_scripts(lua_State* lua, const char* paths)
{
    if (paths == nullptr || paths[0] == '\0')
        return;

    str<> token;
    str_tokeniser tokens(paths, ";");
    while (tokens.next(token))
        load_lua_script(lua, token.c_str());
}

//------------------------------------------------------------------------------
static void initialise_line_editor(lua_State* lua, const char* host_name)
{
    terminal* terminal = new ecma48_terminal();
    match_printer* printer = new column_printer(terminal);

    line_editor::desc desc = { host_name, terminal, printer };
    g_line_editor = create_rl_line_editor(desc);

    // MODE4 - memory leaks!
    // Give the line editor some match generators.
    match_system& match_system = g_line_editor->get_match_system();

    lua_match_generator* lua_generator = new lua_match_generator(lua);
    match_system.add_generator(lua_generator, 1000);

    file_match_generator* file_generator = new file_match_generator();
    match_system.add_generator(file_generator, 1001);
    // MODE4
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

    fprintf(stderr, "Failed to load Clink.\nSee log for details (%s).\n", buffer);
}

//------------------------------------------------------------------------------
static bool get_host_name(str_base& out)
{
    str<MAX_PATH> buffer;
    if (GetModuleFileName(nullptr, buffer.data(), buffer.size()) == buffer.size())
        return false;

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return false;

    return path::get_name(buffer.c_str(), out);
}

//------------------------------------------------------------------------------
int initialise_clink(const inject_args* inject_args)
{
    seh_scope seh;

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

    // Initialise Lua.
    g_lua = new lua_root();
    lua_State* lua = g_lua->get_state();

    // Prepare core systems.
    initialise_clink_settings();
    initialise_line_editor(lua, host_name.c_str());
    load_history();

    // Load match generator Lua scripts.
    const char* setting_clink_path = get_clink_setting_str("clink_path");
    load_lua_scripts(lua, setting_clink_path);

    str<> env_clink_path;
    os::get_env("clink_path", env_clink_path);
    load_lua_scripts(lua, env_clink_path.c_str());

    // Search for a supported host.
    struct {
        const char* name;
        host*       (*creator)(lua_State*, line_editor*);
    } hosts[] = {
        {
            "cmd.exe",
            [](lua_State* s, line_editor* e) -> host* { return new host_cmd(s, e); }
        },
        {
            "powershell.exe",
            [](lua_State* s, line_editor* e) -> host* { return new host_ps(s, e); }
        },
    };

    for (int i = 0; i < sizeof_array(hosts); ++i)
    {
        if (stricmp(host_name.c_str(), hosts[i].name) == 0)
        {
            g_host = (hosts[i].creator)(lua, g_line_editor);
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
    seh_scope seh;

    if (g_host == nullptr)
        return;

    g_host->shutdown();

    if (get_clink_setting_int("history_io"))
        load_history();

    save_history();
    shutdown_clink_settings();
    shutdown_line_editor();

    delete g_lua;

    if (logger* logger = logger::get())
        delete logger;
}
