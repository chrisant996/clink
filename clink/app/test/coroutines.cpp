// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"

#include <core/base.h>
#include <core/str.h>
#include <core/path.h>
#include <core/settings.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lua/prompt.h>

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
static void set_autosuggest_async_default()
{
    setting* setting = settings::find("autosuggest.async");
    setting->set();
}

//------------------------------------------------------------------------------
static void set_autosuggest_async(bool state)
{
    setting* setting = settings::find("autosuggest.async");
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
TEST_CASE("Lua coroutines.")
{
    set_autosuggest_async(false);

    SECTION("Main")
    {
        lua_state lua;
        prompt_filter prompt_filter(lua);
        lua_load_script(lua, app, prompt);

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
            return verify_true(not _yieldguard:ready() and _command == '1aaa', function () print_var('dur', dur) end)\
        end\
        \
        function verify_notready_3aaa()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard:ready() and _command == '3aaa', function () print_var('dur', dur) end)\
        end\
        \
        function verify_notready_3bbb()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard:ready() and _command == '3bbb', function () print_var('dur', dur) end)\
        end\
        \
        function verify_notready_3ccc()\
            local dur = clink._wait_duration()\
            return verify_true(not _yieldguard:ready() and _command == '3ccc', function () print_var('dur', dur) end)\
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
            if clink._has_coroutines() ~= true then\
                return true\
            end\
            report_internals()\
            return false\
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

    SECTION("Coroutine state")
    {
        fs_fixture fs;

        lua_state lua;
        lua_load_script(lua, app, prompt);

        const char* script = "\
        CO_expected = {}\
        CO_actual = {}\
        MN_expected = {}\
        MN_actual = {}\
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
        local function report_internals()\
            print()\
            if diag_func then\
                diag_func()\
            end\
            clink._diag_coroutines()\
        end\
        \
        local function report_CO()\
            print('CO_expected:', 'rs', CO_expected.rs, 'als', CO_expected.als, 'cwd', CO_expected.cwd)\
            print('CO_actual:', 'rs', CO_actual.rs, 'als', CO_actual.als, 'cwd', CO_actual.cwd)\
        end\
        \
        local function report_MN()\
            print('MN_expected:', 'rs', MN_expected.rs, 'als', MN_expected.als, 'cwd', MN_expected.cwd)\
            print('MN_actual:', 'rs', MN_actual.rs, 'als', MN_actual.als, 'cwd', MN_actual.cwd)\
        end\
        \
        local function report_CO_MN()\
            report_CO()\
            report_MN()\
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
        local function verify_result(expected, actual)\
            return (actual.cwd == expected.cwd and\
                    actual.rl_state == expected.rl_state and\
                    actual.argmatcher_line_states == expected.argmatcher_line_states)\
        end\
        \
        function verify_CO_result()\
            return verify_true(verify_result(CO_expected, CO_actual), report_CO)\
        end\
        \
        function verify_MN_result()\
            return verify_true(verify_result(MN_expected, MN_actual), report_MN)\
        end\
        \
        function verify_different()\
            return verify_true(CO_actual.cwd ~= MN_actual.cwd and\
                               CO_actual.rs ~= MN_actual.rs and\
                               CO_actual.als ~= MN_actual.als, report_CO_MN)\
        end\
        \
        local function co_func()\
            clink.co_state.argmatcher_line_states = { 77 }\
            CO_actual.cwd = os.getcwd()\
            CO_actual.rs = rl_state[1]\
            CO_actual.als = clink.co_state.argmatcher_line_states[1]\
        end\
        \
        rl_state = { 11 }\
        clink.co_state.argmatcher_line_states = { 12 }\
        \
        CO_expected.cwd = os.getcwd()\
        CO_expected.rs = 11\
        CO_expected.als = 77\
        \
        coroutine.override_isgenerator()\
        local co = coroutine.create(co_func)\
        \
        os.chdir('dir1')\
        rl_state = { 33 }\
        clink.co_state.argmatcher_line_states = { 34 }\
        \
        MN_expected.cwd = os.getcwd()\
        MN_expected.rs = 33\
        MN_expected.als = 34\
        \
        local _, err = coroutine.resume(co)\
        if err then\
            error(err)\
        end\
        \
        MN_actual.cwd = os.getcwd()\
        MN_actual.rs = rl_state[1]\
        MN_actual.als = clink.co_state.argmatcher_line_states[1]\
        ";

        REQUIRE(lua.do_string(script));

        SECTION("Swap")
        {
            str<> out;

            REQUIRE(out.equals(""));
            REQUIRE(verify_ret_true(lua, "verify_MN_result"));
            REQUIRE(verify_ret_true(lua, "verify_CO_result"));
            REQUIRE(verify_ret_true(lua, "verify_different"));
        }
    }

    set_autosuggest_async_default();
}
