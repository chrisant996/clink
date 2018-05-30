// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host_lua.h"
#include "utils/app_context.h"

#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
static setting_str g_clink_path(
    "clink.path",
    "Paths to load Lua completion scripts from",
    "These paths will be searched for Lua scripts that provide custom\n"
    "match generation. Multiple paths should be delimited by semicolons.",
    "");

//------------------------------------------------------------------------------
host_lua::host_lua()
: m_generator(m_state)
{
    str<280> bin_path;
    app_context::get()->get_binaries_dir(bin_path);

    str<280> exe_path;
    exe_path << bin_path << "\\" CLINK_EXE;

    lua_State* state = m_state.get_state();
    lua_pushstring(state, exe_path.c_str());
    lua_setglobal(state, "CLINK_EXE");
}

//------------------------------------------------------------------------------
host_lua::operator lua_state& ()
{
    return m_state;
}

//------------------------------------------------------------------------------
host_lua::operator match_generator& ()
{
    return m_generator;
}

//------------------------------------------------------------------------------
void host_lua::load_scripts()
{
    const char* setting_clink_path = g_clink_path.get();
    load_scripts(setting_clink_path);

    str<256> env_clink_path;
    os::get_env("clink_path", env_clink_path);
    load_scripts(env_clink_path.c_str());
}

//------------------------------------------------------------------------------
void host_lua::load_scripts(const char* paths)
{
    if (paths == nullptr || paths[0] == '\0')
        return;

    str<280> token;
    str_tokeniser tokens(paths, ";");
    while (tokens.next(token))
        load_script(token.c_str());
}

//------------------------------------------------------------------------------
void host_lua::load_script(const char* path)
{
    str<280> buffer;
    path::join(path, "*.lua", buffer);

    globber lua_globs(buffer.c_str());
    lua_globs.directories(false);

    while (lua_globs.next(buffer))
        m_state.do_file(buffer.c_str());
}



