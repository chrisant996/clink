// Copyright (c) 2022-2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/os.h>
#include <core/settings.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_word_classifier.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <lib/cmd_tokenisers.h>
#include <lib/doskey.h>

extern "C" {
#include <lua.h>
}

//------------------------------------------------------------------------------
static bool verify_ret_true(lua_state& lua, const char* func_name, int32 nargs)
{
    lua_State *state = lua.get_state();

    str<> msg;
    if (!lua.push_named_function(state, func_name, &msg))
    {
        puts("");
        puts(msg.c_str());
        return false;
    }

    // Move function before arguments.
    if (nargs > 0)
        lua_insert(state, 0 - (nargs + 1));

    bool success = (lua.pcall_silent(nargs, 1) == LUA_OK);
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
struct onarg_case
{
    int32 arg_index;
    const char* word;
    int32 word_index;
};

//------------------------------------------------------------------------------
void verify_data(lua_state& lua, const onarg_case* cases, size_t count)
{
    lua_State* L = lua.get_state();

    lua_pushinteger(L, count);
    REQUIRE(verify_ret_true(lua, "verify_count", 1));

    for (size_t index = 0; index < count; ++index)
    {
        lua_pushinteger(L, index + 1);
        lua_pushinteger(L, cases[index].arg_index);
        lua_pushstring(L, cases[index].word);
        lua_pushinteger(L, cases[index].word_index);
        REQUIRE(verify_ret_true(lua, "verify_data", 4));
    }

    REQUIRE(verify_ret_true(lua, "reset_data", 0));
}

//------------------------------------------------------------------------------
TEST_CASE("Lua advanced arg parsers")
{
    fs_fixture fs;

    setting* setting = settings::find("match.translate_slashes");
    setting->set("system");

    lua_state lua;
    lua_match_generator lua_generator(lua);

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;

    line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
    desc.command_tokeniser = &command_tokeniser;
    desc.word_tokeniser = &word_tokeniser;
    line_editor_tester tester(desc, nullptr, nullptr);
    tester.get_editor()->set_generator(lua_generator);

    SECTION("Adaptive")
    {
        const char* script = "\
            local generation = 0\
            local reset = true\
            \
            clink.onbeginedit(function ()\
                generation = generation + 1\
            end)\
            \
            clink.onendedit(function (line)\
                if line:find('reset') then\
                    reset = true\
                end\
            end)\
            \
            local function ondelayinitarg(matcher, argindex)\
                return { 'mb'..generation..'('..argindex..')' }\
            end\
            \
            local function ondelayinit(matcher)\
                if reset then\
                    reset = false\
                    matcher:reset()\
                    matcher:addarg({ 'aa'..generation, 'ab'..generation, 'zz'..generation })\
                    matcher:addarg({ delayinit=ondelayinitarg, 'ma' })\
                end\
            end\
            \
            adapt = clink.argmatcher('adapt'):setdelayinit(ondelayinit)\
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        SECTION("Delayinit argmatcher")
        {
            lua.send_event("onbeginedit");
            tester.set_input("adapt \x1b*");
            tester.set_expected_output("adapt aa1 ab1 zz1 ");
            tester.run();
            lua_pushlstring(lua.get_state(), "adapt", 5);
            lua.send_event("onendedit", 1);

            lua.send_event("onbeginedit");
            tester.set_input("adapt \x1b*");
            tester.set_expected_output("adapt aa1 ab1 zz1 ");
            tester.run();
            lua_pushlstring(lua.get_state(), "reset", 5);
            lua.send_event("onendedit", 1);

            lua.send_event("onbeginedit");
            tester.set_input("adapt \x1b*");
            tester.set_expected_output("adapt aa3 ab3 zz3 ");
            tester.run();
            lua_pushlstring(lua.get_state(), "adapt", 5);
            lua.send_event("onendedit", 1);
        }

        SECTION("Delayinit argument position")
        {
            lua.send_event("onbeginedit");
            tester.set_input("adapt x \x1b*");
            tester.set_expected_output("adapt x ma mb1(2) ");
            tester.run();
            lua_pushlstring(lua.get_state(), "adapt", 5);
            lua.send_event("onendedit", 1);

            lua.send_event("onbeginedit");
            tester.set_input("adapt x \x1b*");
            tester.set_expected_output("adapt x ma mb1(2) ");
            tester.run();
            lua_pushlstring(lua.get_state(), "reset", 5);
            lua.send_event("onendedit", 1);

            lua.send_event("onbeginedit");
            tester.set_input("adapt x \x1b*");
            tester.set_expected_output("adapt x ma mb3(2) ");
            tester.run();
            lua_pushlstring(lua.get_state(), "adapt", 5);
            lua.send_event("onendedit", 1);
        }
    }

    SECTION("Chaincommand")
    {
        // TODO: Also fromhistory tests, since the code path is different.

        static const char* dir_fs[] = {
#ifdef FIXED_FIND_ARGMATCHER_DIRNAME
            "cd/leaf",              // Tricksy!  And caught an issue in _has_argmatcher.
#endif
            "nest/leaf",
            "nest/nest2/leaf",
            nullptr,
        };

        fs_fixture fs(dir_fs);

        const char* script = "\
            clink.argmatcher('sudo'):addflags('-x', '/x'):chaincommand()\
            clink.argmatcher('gsudo'):addflags('-y', '/y'):chaincommand()\
            clink.argmatcher('plerg'):addflags('-l', '-m', '-n', '/l', '/m', '/n'):addarg('aaa', 'zzz'):nofiles()\
            clink.argmatcher('cd'):addflags('/d'):addarg(clink.dirmatches):nofiles()\
        ";

        lua_load_script(lua, app, exec);

        REQUIRE_LUA_DO_STRING(lua, script);

        str<> host(os::get_shellname());
        doskey doskey(host.c_str());

        MAKE_CLEANUP([&doskey](){
            doskey.remove_alias("xyz");
        });

        SECTION("Flag at end")
        {
            REQUIRE(doskey.add_alias("xyz", "sudo gsudo plerg -m $*") == true);

            tester.set_input("xyz ");
            tester.set_expected_matches("aaa", "zzz");
            tester.run();

            tester.set_input("xyz -");
            tester.set_expected_matches("-l", "-m", "-n");
            tester.run();

            tester.set_input("xyz 123 ");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Flag in middle")
        {
            REQUIRE(doskey.add_alias("xyz", "sudo gsudo -y plerg $*") == true);

            tester.set_input("xyz ");
            tester.set_expected_matches("aaa", "zzz");
            tester.run();

            tester.set_input("xyz -");
            tester.set_expected_matches("-l", "-m", "-n");
            tester.run();

            tester.set_input("xyz 123 ");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("Slash in command")
        {
            tester.set_input("plerg /");
            tester.set_expected_matches("/l", "/m", "/n");
            tester.run();

            tester.set_input("plerg/");
            tester.set_expected_matches("/l", "/m", "/n");
            tester.run();

            tester.set_input("cd /");
            tester.set_expected_matches("/d");
            tester.run();

            tester.set_input("cd/");
            tester.set_expected_matches("/d");
            tester.run();

            tester.set_input("sudo plerg /");
            tester.set_expected_matches("/l", "/m", "/n");
            tester.run();

            tester.set_input("sudo plerg/");
            tester.set_expected_matches("/l", "/m", "/n");
            tester.run();

            tester.set_input("sudo cd /");
            tester.set_expected_matches("/d");
            tester.run();

            tester.set_input("sudo cd/");
            tester.set_expected_matches("/d");
            tester.run();

            tester.set_input("sudo gsudo cd /");
            tester.set_expected_matches("/d");
            tester.run();

            tester.set_input("sudo gsudo cd/");
            tester.set_expected_matches("/d");
            tester.run();
        }

        SECTION("Slash in word")
        {
#ifdef FIXED_FIND_ARGMATCHER_DIRNAME
            tester.set_input("abcdefg cd/");
            tester.set_expected_matches("leaf");
            tester.run();
#endif

            tester.set_input("abcdefg nest/");
            tester.set_expected_matches("nest\\leaf", "nest\\nest2\\");
            tester.run();

            tester.set_input("abcdefg ne/");
            tester.set_expected_matches();
            tester.run();
        }

        SECTION("No expand doskey")
        {
            // TODO: Ensure breaking `foo/` into `foo` and `/` doesn't allow expanding a doskey alias `foo`.
        }
    }

    SECTION("nowordbreakchars")
    {
        // TODO: Also a fromhistory matcher, since the code path is slightly different.

        static const char* bat_fs[] = {
            "bat_thing.bat",
            nullptr,
        };

        fs_fixture fs_bat(bat_fs);

        const char* script = "\
            local wonky = clink.argmatcher():addarg({nowordbreakchars='','12,34','12,35','12,42','3psych'})\
            local wonderful = clink.argmatcher():addarg({nowordbreakchars=',','12,34','12,35','12,42','3psych'})\
            local automatic = clink.argmatcher():addarg({'12,34','12,35','12,42','3psych'})\
            clink.argmatcher('wt_thing'):addflags('-,', '-x'..wonky, '-y'..wonderful, '-z'..automatic):addarg('abc', '3ohnoes')\
            clink.argmatcher('bat_thing'):addflags('-,', '-x'..wonky, '-y'..wonderful, '-z'..automatic):addarg('abc', '3ohnoes')\
        ";

        REQUIRE_LUA_DO_STRING(lua, script);

        SECTION("Comma : yes")
        {
            tester.set_input("wt_thing -x 12,3");
            tester.set_expected_matches("3ohnoes");
            tester.run();

            tester.set_input("wt_thing -x 12,");
            tester.set_expected_matches("abc", "3ohnoes");
            tester.run();
        }

        SECTION("Comma : no")
        {
            tester.set_input("wt_thing -y 12,3");
            tester.set_expected_matches("12,34", "12,35");
            tester.run();

            tester.set_input("wt_thing -y 12,");
            tester.set_expected_matches("12,34", "12,35", "12,42");
            tester.run();
        }

        SECTION("Comma : auto exe")
        {
            tester.set_input("wt_thing -,");
            tester.set_expected_matches("-,");
            tester.run();

            tester.set_input("wt_thing -z 12,3");
            tester.set_expected_matches("3ohnoes");
            tester.run();

            tester.set_input("wt_thing -z 12,");
            tester.set_expected_matches("abc", "3ohnoes");
            tester.run();
        }

        SECTION("Comma : auto bat")
        {
            tester.set_input("bat_thing -,");
            tester.set_expected_matches("abc", "3ohnoes");
            tester.run();

            tester.set_input("bat_thing -z 12,3");
            tester.set_expected_matches("3ohnoes");
            tester.run();

            tester.set_input("bat_thing -z 12,");
            tester.set_expected_matches("abc", "3ohnoes");
            tester.run();
        }
    }

    SECTION("Callbacks")
    {
        const char* script = "\
            local function maybe_string(ai, w, word_index, line_state, _)\
                local info = line_state:getwordinfo(word_index)\
                if not info.quoted then\
                    return 1\
                end\
            end\
            local xargs = clink.argmatcher():addarg('aaa', 'bbb')\
            clink.argmatcher('start'):addarg({onadvance=maybe_string}):addflags('-x'..xargs):chaincommand()\
            clink.argmatcher('plerg'):addflags('-l', '-m', '-n'):addarg('xxx', 'yyy'):nofiles()\
            \
            local function maybe_chain(ai, word, word_index, line_state, user_data)\
                if user_data.do_chain then\
                    return -1\
                elseif word == 'chain' then\
                    user_data.do_chain = true\
                    return 0\
                elseif path.getextension(word) ~= '' then\
                    return -1\
                end\
            end\
            local numbers = clink.argmatcher():addarg('11', '22')\
            local function maybe_link(l, ai, word)\
                if ai == 0 and word == '-f' then\
                    return numbers\
                elseif ai > 0 and word == 'three' then\
                    return false\
                elseif ai > 0 and word == 'link' then\
                    return numbers\
                end\
            end\
            clink.argmatcher('qqq')\
            :addarg({onadvance=maybe_chain, onlink=maybe_link, 'one', 'two'..numbers, 'three'..numbers})\
            :addflags({onlink=maybe_link, '-x'})\
            :nofiles()\
        ";

        lua_load_script(lua, app, cmd);
        lua_load_script(lua, app, dir);
        lua_load_script(lua, app, exec);
        lua_word_classifier lua_classifier(lua);
        tester.get_editor()->set_classifier(lua_classifier);

        MAKE_CLEANUP([](){
            settings::find("clink.colorize_input")->set("false");
            settings::find("color.argmatcher")->set();
        });

        settings::find("clink.colorize_input")->set("true");
        settings::find("color.argmatcher")->set("92");

        REQUIRE_LUA_DO_STRING(lua, script);

        SECTION("onadvance")
        {
            tester.set_input("start -x ");
            tester.set_expected_classifications("mcf", true);
            tester.set_expected_matches("aaa", "bbb");
            tester.run();

            tester.set_input("start qq plerg f");
            tester.set_expected_classifications("mco", true);
            tester.set_expected_matches("file1", "file2");
            tester.run();

            tester.set_input("start \"qq\" plerg ");
            tester.set_expected_classifications("mcomo", true);
            tester.set_expected_matches("xxx", "yyy");
            tester.run();

            tester.set_input("start \"qq\" plerg -l ");
            tester.set_expected_classifications("mcomof", true);
            tester.set_expected_matches("xxx", "yyy");
            tester.run();
        }

        SECTION("onadvance chain")
        {
            tester.set_input("qqq one");
            tester.set_expected_classifications("moa", true);
            tester.run();

            tester.set_input("qqq chain cmd");
            tester.set_expected_classifications("moomo", true);
            tester.run();

            tester.set_input("qqq cmd");
            tester.set_expected_classifications("moo", true);
            tester.run();

            tester.set_input("qqq cmd.exe");
            tester.set_expected_classifications("momo", true);
            tester.run();
        }

        SECTION("onlink")
        {
            tester.set_input("qqq ");
            tester.set_expected_matches("one", "two", "three");
            tester.run();

            tester.set_input("qqq one ");
            tester.set_expected_matches();
            tester.run();

            tester.set_input("qqq two ");
            tester.set_expected_matches("11", "22");
            tester.run();

            tester.set_input("qqq three ");
            tester.set_expected_matches();
            tester.run();

            tester.set_input("qqq link ");
            tester.set_expected_matches("11", "22");
            tester.run();

            tester.set_input("qqq -x ");
            tester.set_expected_matches("one", "two", "three");
            tester.run();

            tester.set_input("qqq -f ");
            tester.set_expected_matches("11", "22");
            tester.run();
        }
    }

    SECTION("onalias")
    {
        const char* script = "\
            local function expand(arg_index, word, word_index, line_state, user_data)\
                if word == 'c' then return 'start -x', true end\
                if word == 'chain' then return 'start -x', true end\
                if word == 'a' then return 'add' end\
                if word == 'az' then return 'add -z' end\
            end\
            \
            local xargs = clink.argmatcher():addarg('aaa', 'bbb')\
            clink.argmatcher('start'):addarg({onadvance=maybe_string}):addflags('-x'..xargs):chaincommand()\
            \
            local zflag_parser = clink.argmatcher():addarg({'red', 'green', 'blue'})\
            local add_parser = clink.argmatcher():addarg({'one', 'two', 'three'}):addflags({'-x', '-y', '-z'..zflag_parser}):nofiles()\
            \
            clink.argmatcher('qqq')\
            :addarg({onalias=expand, 'add'..add_parser, 'chain'})\
            :addflags({'-a', '-b', '-c'})\
            :nofiles()\
        ";

        lua_load_script(lua, app, cmd);
        lua_load_script(lua, app, dir);
        lua_load_script(lua, app, exec);
        lua_word_classifier lua_classifier(lua);
        tester.get_editor()->set_classifier(lua_classifier);

        doskey doskey("clink_test_harness");

        MAKE_CLEANUP([&doskey](){
            settings::find("clink.colorize_input")->set("false");
            settings::find("color.argmatcher")->set();
            //doskey.remove_alias("qadd_");
            doskey.remove_alias("qadd");
            doskey.remove_alias("qa");
            doskey.remove_alias("qaz");
        });

        settings::find("clink.colorize_input")->set("true");
        settings::find("color.argmatcher")->set("92");

        // doskey.add_alias("qadd_", "qqq add");
        doskey.add_alias("qadd", "qqq add $*");
        doskey.add_alias("qa", "qqq a $*");
        doskey.add_alias("qaz", "qqq az $*");

        REQUIRE_LUA_DO_STRING(lua, script);

        tester.set_input("qqq add ");
        tester.set_expected_classifications("moa", true);
        tester.set_expected_matches("one", "two", "three");
        tester.run();

        tester.set_input("qqq a ");
        tester.set_expected_classifications("moo", true);
        tester.set_expected_matches("one", "two", "three");
        tester.run();

        // tester.set_input("qadd_ o");
        // tester.set_expected_classifications("mo", true);
        // tester.set_expected_matches("one", "two", "three");
        // tester.run();

        tester.set_input("qadd o");
        tester.set_expected_classifications("mdo", true);
        tester.set_expected_matches("one");
        tester.run();

        tester.set_input("qa o");
        tester.set_expected_classifications("mdo", true);
        tester.set_expected_matches("one");
        tester.run();

        tester.set_input("qqq add -z ");
        tester.set_expected_classifications("moaf", true);
        tester.set_expected_matches("red", "green", "blue");
        tester.run();

        tester.set_input("qqq az ");
        tester.set_expected_classifications("moo", true);
        tester.set_expected_matches("red", "green", "blue");
        tester.run();

        tester.set_input("qaz ");
        // It seems like "md" would be more correct, but even before the
        // argmatcher next_word refactor in commit 7b46891e76efd6dd077f this
        // "o" quirk was present:
        //  - "somealias " yields "mdo".
        //  - "actualpgm " yields "mo".
        //  - "actualpgm  " yields "moo".
        // tester.set_expected_classifications("md", true);
        tester.set_expected_classifications("mdo", true);
        tester.set_expected_matches("red", "green", "blue");
        tester.run();

        tester.set_input("qqq chain ");
        tester.set_expected_classifications("moa", true);
        tester.set_expected_matches("aaa", "bbb");
        tester.run();

        tester.set_input("qqq c ");
        tester.set_expected_classifications("moo", true);
        tester.set_expected_matches("aaa", "bbb");
        tester.run();
    }

    SECTION("onarg")
    {
        const char* script = "\
            local data = {}\
            \
            function reset_data()\
                data = {}\
                return true\
            end\
            \
            function verify_data(index, arg_index, word, word_index)\
                if not data[index] then\
                    print(string.format('\\n\\nmissing data for index %d', index))\
                    return false\
                end\
                if data[index][1] ~= data[index][2] then\
                    print(string.format('\\n\\narg index mismatch %d vs %d', data[index][1], data[index][2]))\
                    return false\
                end\
                if data[index][2] ~= arg_index then\
                    print(string.format('\\n\\nunexpected arg index %d; should be %d', data[index][2], arg_index))\
                    return false\
                end\
                if data[index][3] ~= word then\
                    print(string.format('\\n\\nunexpected word \'%s\'; should be \'%s\'', data[index][3], word))\
                    return false\
                end\
                if data[index][4] ~= word_index then\
                    print(string.format('\\n\\nunexpected word_index %d; should be %d', data[index][4], word_index))\
                    return false\
                end\
                return true\
            end\
            \
            function verify_count(count)\
                if count ~= #data then\
                    print(string.format('\\n\\nunexpected data count %d; should be %d', #data, count))\
                    return false\
                end\
                return true\
            end\
            \
            local function onarg1(arg_index, word, word_index, line_state, user_data)\
                table.insert(data, { 1, arg_index, word, word_index })\
            end\
            \
            local function onarg2(arg_index, word, word_index, line_state, user_data)\
                table.insert(data, { 2, arg_index, word, word_index })\
            end\
            \
            local function onarg3(arg_index, word, word_index, line_state, user_data)\
                table.insert(data, { 3, arg_index, word, word_index })\
            end\
            \
            local function onflag(arg_index, word, word_index, line_state, user_data)\
                table.insert(data, { 0, arg_index, word, word_index })\
            end\
            \
            clink.argmatcher('qqq')\
            :addarg({ onarg=onarg1 })\
            :addarg({ onarg=onarg2 })\
            :addarg({ onarg=onarg3 })\
            :addflags({ onarg=onflag, '-a', '-b', '-c' })\
            :loop()\
        ";

        lua_load_script(lua, app, cmd);
        lua_load_script(lua, app, dir);
        lua_load_script(lua, app, exec);

        REQUIRE_LUA_DO_STRING(lua, script);

        {
            static const onarg_case c_cases[] =
            {
                { 1, "one", 2 },
            };
            tester.set_input("qqq one ");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases));

            tester.set_input("qqq one");
            tester.run(true);
            verify_data(lua, nullptr, 0);
        }

        {
            static const onarg_case c_cases[] =
            {
                { 1, "one", 2 },
                { 2, "two", 3 },
            };
            tester.set_input("qqq one two ");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases));

            tester.set_input("qqq one two");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases) - 1);
        }

        {
            static const onarg_case c_cases[] =
            {
                { 1, "one", 2 },
                { 0, "-a", 3 },
                { 0, "-z", 4 },
                { 2, "two", 5 },
            };
            tester.set_input("qqq one -a -z two ");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases));

            tester.set_input("qqq one -a -z two");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases) - 1);
        }

        {
            static const onarg_case c_cases[] =
            {
                { 0, "-b", 2 },
                { 1, "one", 3 },
                { 0, "-z", 4 },
                { 2, "two", 5 },
                { 3, "three", 6 },
                { 1, "four", 7 },
                { 0, "-m", 8 },
                { 2, "five", 9 },
                { 3, "six", 10 },
                { 0, "-c", 11 },
                { 1, "seven", 12 },
            };
            tester.set_input("qqq -b one -z two three four -m five six -c seven ");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases));

            tester.set_input("qqq -b one -z two three four -m five six -c seven");
            tester.run(true);
            verify_data(lua, c_cases, _countof(c_cases) - 1);
        }
    }

    setting->set();
}
