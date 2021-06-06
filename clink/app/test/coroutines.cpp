// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "../src/host/prompt.h"

#include <core/base.h>
#include <core/str.h>
#include <core/path.h>
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

    bool success = (lua.pcall(0, 1) == LUA_OK);
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
static bool get_yieldguard_ready(lua_state& lua, str_base& out)
{
    // TODO: pcall get_yieldguard_ready
    // TODO: out = first ret
    // TODO: return second ret
}

//------------------------------------------------------------------------------
TEST_CASE("Lua coroutines.")
{
    lua_state lua;
    prompt_filter prompt_filter(lua);
    lua_load_script(lua, app, prompt);

    SECTION("Main")
    {
        const char* script = "\
        function reset_coroutine_test()\
            _yieldguard_ready = false\
            _ran = ''\
            _command = ''\
            _gen = 0\
            return true\
        end\
        \
        function io.popenyield_internal(command, mode)\
            local yieldguard = {}\
            function yieldguard:ready()\
                return _yieldguard_ready\
            end\
            _yieldguard_ready = false\
            _ran = _ran..'|'..command\
            _command = command\
            return 'fake_file', yieldguard\
        end\
        \
        function verify_wait_duration_nil()\
            return clink._wait_duration() == nil\
        end\
        \
        function verify_resume_coroutines()\
            if clink._has_coroutines() then\
                clink._resume_coroutines()\
                return true\
            end\
        end\
        \
        function verify_notready_1aaa()\
            return not _yieldguard_ready and _command == '1aaa'\
        end\
        \
        function verify_notready_3aaa()\
print('_yg_ready', _yieldguard_ready)\
print('_command', _command)\
            return not _yieldguard_ready and _command == '3aaa'\
        end\
        \
        function verify_notready_3bbb()\
            return not _yieldguard_ready and _command == '3bbb'\
        end\
        \
        function verify_notready_3ccc()\
            return not _yieldguard_ready and _command == '3ccc'\
        end\
        \
        function verify_ran_correct_commands()\
            return _ran == '|1aaa|3aaa|3bbb|3ccc'\
        end\
        \
        function set_yieldguard_ready()\
            _yieldguard_ready = true\
            return true\
        end\
        \
        local function init()\
            local gen = _gen\
            local f,yg\
            f,yg = io.popenyield(gen..'aaa')\
            f,yg = io.popenyield(gen..'bbb')\
            f,yg = io.popenyield(gen..'ccc')\
            return gen..'zzz'\
        end\
        \
        local pf = clink.promptfilter(1)\
        function pf:filter(prompt)\
            _gen = _gen + 1\
            return clink.promptcoroutine(init)\
        end\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Overlap")
        {
            str<> out;
            REQUIRE(verify_ret_true(lua, "reset_coroutine_test"));

            // Simulate prompt 1.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals(""));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_1aaa"));
            REQUIRE(verify_ret_true(lua, "verify_wait_duration_nil"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_1aaa"));

            // Simulate prompt 2.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals(""));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_1aaa"));

            // Simulate prompt 3.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals(""));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_1aaa"));

#if 0
            // Allow first popenyield from prompt 1 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_3aaa"));

            // Allow first popenyield from prompt 3 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_3bbb"));

            // Allow second popenyield from prompt 3 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_3ccc"));

            // Allow third popenyield from prompt 3 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_ran_correct_commands"));

// TODO: verify coroutine is dead and yg ready (?) and refilter is true

            // Simulate refilter, which should produce the expected final result.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals("3zzz"));

// TODO: verify clink._has_coroutines() returns false
            REQUIRE(verify_ret_true(lua, "verify_ran_correct_commands"));
#endif
        }
    }
}
