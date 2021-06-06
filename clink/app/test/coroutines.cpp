// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "../src/host/prompt.h"

#include <core/base.h>
#include <core/str.h>
#include <core/path.h>
#include <core/settings.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
static void set_prompt_async_default()
{
    setting* setting = settings::find("prompt.async");
    setting->set();
}

//------------------------------------------------------------------------------
static void set_prompt_async(bool state)
{
    setting* setting = settings::find("prompt.async");
    setting->set(state ? "true" : "false");
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
TEST_CASE("Lua coroutines.")
{
    lua_state lua;
    prompt_filter prompt_filter(lua);
    lua_load_script(lua, app, prompt);

    SECTION("Main")
    {
        const char* script = "\
        function reset_coroutine_test()\
            _refilter = false\
            _yieldguard = nil\
            _ran = ''\
            _command = ''\
            _gen = 0\
            return true\
        end\
        \
        function clink.refilterprompt()\
            _refilter = true\
        end\
        \
        function io.popenyield_internal(command, mode)\
            local yieldguard = { _ready=false, _command=command }\
            function yieldguard:ready()\
                return self._ready\
            end\
            function yieldguard:setready()\
                self._ready = true\
            end\
            function yieldguard:command()\
                return self._command\
            end\
            _yieldguard = yieldguard\
            _ran = _ran..'|'..command\
            _command = command\
            return 'fake_file', yieldguard\
        end\
        \
        function io.popen(command, mode)\
            _ran = _ran..'|'..command\
            return 'fake_file'\
        end\
        \
        function verify_wait_duration_nil()\
            return clink._wait_duration() == nil\
        end\
        \
        local function str_rpad(s, width, pad)\
            if width <= #s then\
                return s\
            end\
            return s..string.rep(pad or ' ', width - #s)\
        end\
        \
        local function print_var(name, value)\
            print(str_rpad(name, 15), value)\
        end\
        \
        local function report_internals(diag_func)\
            print()\
            if diag_func then\
                diag_func()\
            end\
            print_var('_refilter', _refilter)\
            print_var('_yg_ready', (not _yieldguard and 'nil yg') or (_yieldguard:ready() and 'ready') or 'false')\
            print_var('_ran', _ran)\
            print_var('_command', _command)\
            print_var('_gen', _gen)\
            clink._diag_coroutines()\
        end\
        \
        local function verify_true(value, diag_func)\
            if value then\
                return true\
            else\
                report_internals(diag_func)\
            end\
        end\
        \
        function verify_resume_coroutines()\
            local dur = clink._wait_duration()\
            if clink._has_coroutines() then\
                clink._resume_coroutines()\
                return true\
            end\
            report_internals()\
        end\
        \
        function verify_notready_1aaa()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard_ready and _command == '1aaa', function () print_var('dur', dur) end)\
        end\
        \
        function verify_notready_3aaa()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard_ready and _command == '3aaa', function () print_var('dur', dur) end)\
        end\
        \
        function verify_notready_3bbb()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard_ready and _command == '3bbb', function () print_var('dur', dur) end)\
        end\
        \
        function verify_notready_3ccc()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard_ready and _command == '3ccc', function () print_var('dur', dur) end)\
        end\
        \
        function verify_not_refilter()\
            return not _refilter\
        end\
        \
        function verify_refilter()\
            return _refilter\
        end\
        \
        function verify_ran_non_orphaned_commands()\
            return _ran == '|1aaa|3aaa|3bbb|3ccc'\
        end\
        \
        function verify_ran_commands_1_disabled()\
            return _ran == '|1aaa|1bbb|1ccc'\
        end\
        \
        function verify_ran_commands_2_disabled()\
            return _ran == '|1aaa|1bbb|1ccc|2aaa|2bbb|2ccc'\
        end\
        \
        function verify_ran_commands_3_disabled()\
            return _ran == '|1aaa|1bbb|1ccc|2aaa|2bbb|2ccc|3aaa|3bbb|3ccc'\
        end\
        \
        function verify_no_coroutines()\
            return clink._has_coroutines() ~= true\
        end\
        \
        function set_yieldguard_ready()\
            _yieldguard:setready()\
            return true\
        end\
        \
        local function init()\
            local gen = _gen\
            local f\
            f = io.popenyield(gen..'aaa')\
            f = io.popenyield(gen..'bbb')\
            f = io.popenyield(gen..'ccc')\
            return gen..'zzz'\
        end\
        \
        local pf = clink.promptfilter(1)\
        function pf:filter(prompt)\
            _gen = _gen + 1\
            return clink.promptcoroutine(init), false\
        end\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Enabled")
        {
            set_prompt_async(true);

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
            REQUIRE(verify_ret_true(lua, "verify_not_refilter"));

            // Simulate prompt 2.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals(""));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_1aaa"));
            REQUIRE(verify_ret_true(lua, "verify_not_refilter"));

            // Simulate prompt 3.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals(""));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_1aaa"));
            REQUIRE(verify_ret_true(lua, "verify_not_refilter"));

            // Allow first popenyield from prompt 1 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_3aaa"));
            REQUIRE(verify_ret_true(lua, "verify_not_refilter"));

            // Allow first popenyield from prompt 3 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_3bbb"));
            REQUIRE(verify_ret_true(lua, "verify_not_refilter"));

            // Allow second popenyield from prompt 3 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_notready_3ccc"));
            REQUIRE(verify_ret_true(lua, "verify_not_refilter"));

            // Allow third popenyield from prompt 3 to continue.
            REQUIRE(verify_ret_true(lua, "set_yieldguard_ready"));
            REQUIRE(verify_ret_true(lua, "verify_resume_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_ran_non_orphaned_commands"));

            REQUIRE(verify_ret_true(lua, "verify_no_coroutines"));
            REQUIRE(verify_ret_true(lua, "verify_refilter"));

            // Simulate refilter, which should produce the expected final result.
            prompt_filter.filter("", out);
            REQUIRE(out.equals("3zzz"));

            REQUIRE(verify_ret_true(lua, "verify_ran_non_orphaned_commands"));
        }

        SECTION("Disabled")
        {
            set_prompt_async(false);

            str<> out;
            REQUIRE(verify_ret_true(lua, "reset_coroutine_test"));

            // Simulate prompt 1.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals("1zzz"));
            REQUIRE(verify_ret_true(lua, "verify_ran_commands_1_disabled"));

            // Simulate prompt 2.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals("2zzz"));
            REQUIRE(verify_ret_true(lua, "verify_ran_commands_2_disabled"));

            // Simulate prompt 3.
            lua.send_event("onbeginedit");
            prompt_filter.filter("", out);
            REQUIRE(out.equals("3zzz"));
            REQUIRE(verify_ret_true(lua, "verify_ran_commands_3_disabled"));

            REQUIRE(verify_ret_true(lua, "verify_no_coroutines"));
        }
    }

    set_prompt_async_default();
}
