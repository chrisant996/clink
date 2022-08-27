// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"

#include <core/base.h>
#include <core/str.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/os.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
static bool verify_ret_true(lua_state& lua, const char* func_name)
{
    lua_State *state = lua.get_state();

    str<> msg;
    if (!lua.push_named_function(state, func_name, &msg))
    {
        puts("");
        puts(msg.c_str());
        return false;
    }

    bool success = (lua.pcall_silent(0, 1) == LUA_OK);
    if (!success)
    {
        if (const char* error = lua_tostring(state, -1))
        {
            puts("");
            printf("error executing function '%s':\n", func_name);
            puts(error);
        }
        return false;
    }

    if (!lua_isboolean(state, -1))
        return false;

    return lua_toboolean(state, -1);
}

//------------------------------------------------------------------------------
TEST_CASE("Lua io")
{
    settings::find("lua.debug")->set("true");

    fs_fixture fs;

    lua_state lua;

    SECTION("Main")
    {
        const char* script = "\
            function verify_file(name, content) \
                local i = 0 \
                local f = io.open(name, 'r') \
                for line in f:lines() do \
                    i = i + 1 \
                    if line ~= content[i] then \
                        return false \
                    end \
                end \
                f:close() \
                return i == #content \
            end \
            \
            local name = 'lua_io_test.txt' \
            \
            function create_file() \
                local f = io.open(name, 'w+') \
                f:close() \
                return verify_file(name, {}) \
            end \
            \
            function fill_file() \
                local f = io.open(name, 'w') \
                f:write('hello\\n') \
                f:write('world\\n') \
                f:write('123\\n') \
                f:write('abc\\n') \
                f:close() \
                return verify_file(name, {'hello', 'world', '123', 'abc'}) \
            end \
            \
            function truncate_file() \
                local i = 0 \
                local expected = {'hello', 'world', '123', 'abc'} \
                local f = io.open(name, 'r+') \
                for line in f:lines() do \
                    i = i + 1 \
                    if line ~= expected[i] then \
                        return false \
                    end \
                end \
                f:seek('set', 0) \
                f:write('foo\\n') \
                f:write('bar\\n') \
                io.truncate(f) \
                f:close() \
                return verify_file(name, {'foo', 'bar'}) \
            end \
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Truncate")
        {
            REQUIRE(verify_ret_true(lua, "create_file"));
            REQUIRE(verify_ret_true(lua, "fill_file"));
            REQUIRE(verify_ret_true(lua, "truncate_file"));
        }
    }
}
