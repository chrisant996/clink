// Copyright (c) 2020 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/log.h>
#include <core/str.h>

//------------------------------------------------------------------------------
/// -name:  log.info
/// -ver:   1.1.3
/// -arg:   message:string
/// -arg:   [level:integer]
/// Writes info <span class="arg">message</span> to the Clink log file.  Use
/// this sparingly, or it could cause performance problems or disk space
/// problems.
///
/// In v1.4.10 and higher, the optional <span class="arg">level</span> number
/// tells which stack level to log as the source of the log message (default is
/// 1, the function calling <code>log.info</code>).
int log_info(lua_State* state)
{
    const int level = optinteger(state, 2, 1);

    lua_Debug ar = {};
    lua_getstack(state, level, &ar);
    lua_getinfo(state, "Sl", &ar);
    const char* source = ar.source ? ar.source : "?";
    int line = ar.currentline;

    const char* message = checkstring(state, 1);
    if (message)
        logger::info(source, line, "%s", message);

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  log.getfile
/// -ver:   1.4.15
/// -ret:   string | nil
/// Returns the file name of the current session's log file.
int get_file(lua_State* state)
{
    const char* name = file_logger::get_path();
    if (name)
        lua_pushstring(state, name);
    else
        lua_pushnil(state);
    return 1;
}

//------------------------------------------------------------------------------
void log_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "info",        &log_info },
        { "getfile",     &get_file },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "log");
}
