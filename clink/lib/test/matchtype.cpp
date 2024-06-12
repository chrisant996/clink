// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/settings.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <readline/readline.h>
#include <readline/keymaps.h>

//------------------------------------------------------------------------------
static const char script[] =
"local my_generator = clink.generator(10)\n"
"\n"
"local available = {\n"
"    { match = 'foo/bar', type = 'word' },\n"
"    { match = 'foo/bark', type = 'word' },\n"
"    { match = 'foo/box', type = 'word' },\n"
"    { match = 'food', type = 'file' },\n"
"    { match = 'fool', type = 'word' },\n"
"    { match = 'bar', type = 'file' },\n"
"    { match = 'dir', type = 'dir' },\n"
"    { match = 'xyz', type = 'word' }\n"
"}\n"
"\n"
"local addl_dir = {\n"
"    { match = 'dir\\\\blip', type = 'file' },\n"
"    { match = 'dir\\\\boom', type = 'file' },\n"
"}\n"
"\n"
"local available_dir = {\n"
"    { match = 'dir\\\\bark', type = 'file' },\n"
"    { match = 'dir\\\\boxy', type = 'file' },\n"
"    addl_dir,\n"
"}\n"
"\n"
"function string.starts(str, start)\n"
"  return string.sub(str, 1, string.len(start)) == start\n"
"end\n"
"\n"
"function my_generator:generate(line_state, match_builder)\n"
"    local ret = false\n"
"    local matches = nil\n"
"\n"
"    if line_state:getword(1) == 'plugh' then\n"
"        if line_state:getwordcount() == 2 then\n"
"            if line_state:getendword() == 'dir\\\\' or\n"
"                    line_state:getendword() == 'dir/' then\n"
"                matches = available_dir\n"
"            else\n"
"                -- First match pathish discards all non-pathish matches.\n"
"                match_builder:addmatch('dir', 'dir')\n"
"                matches = available\n"
"            end\n"
"        end\n"
"    elseif line_state:getword(1) == 'xyzzy' then\n"
"        if line_state:getwordcount() == 2 then\n"
"            -- First match non-pathish discards all pathish matches.\n"
"            match_builder:addmatch('f', 'word')\n"
"            matches = available\n"
"        end\n"
"    end\n"
"\n"
"    --print('['..line_state:getendword()..'] '..line_state:getwordcount())\n"
"    if matches then\n"
"        for i,v in ipairs(matches) do\n"
"            --print(v.match..' ('..(v.type or 'none')..')')\n"
"            match_builder:addmatch(v)\n"
"            ret = true\n"
"        end\n"
"    end\n"
"\n"
"    return ret\n"
"end\n"
;

//------------------------------------------------------------------------------
static const char *matchtype_fs[] = {
    nullptr,
};

//------------------------------------------------------------------------------
TEST_CASE("Match type : simple")
{
    fs_fixture fs(matchtype_fs);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua.do_string(script, int32(strlen(script)));

    line_editor_tester tester;
    tester.get_editor()->set_generator(lua_generator);

    SECTION("pathish matches")
    {
        tester.set_input("plugh fo");
        tester.set_expected_matches("foo/bar", "foo/bark", "foo/box", "food", "fool");
        tester.run();
    }

    SECTION("non-pathish matches")
    {
        tester.set_input("xyzzy fo");
        tester.set_expected_matches("foo/bar", "foo/bark", "foo/box", "food", "fool");
        tester.run();
    }

    SECTION("pathish readline")
    {
        tester.set_input("plugh fo\x1b*");
        tester.set_expected_output("plugh foo/bar foo/bark foo/box food fool ");
        tester.run();
    }

    SECTION("non-pathish readline")
    {
        tester.set_input("xyzzy fo\x1b*");
        tester.set_expected_output("xyzzy foo/bar foo/bark foo/box food fool ");
        tester.run();
    }
}

//------------------------------------------------------------------------------
TEST_CASE("Match type : slash")
{
    fs_fixture fs(matchtype_fs);

    setting* setting = settings::find("match.translate_slashes");
    setting->set("system");

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua.do_string(script, int32(strlen(script)));

    line_editor_tester tester;
    tester.get_editor()->set_generator(lua_generator);

    SECTION("pathish matches")
    {
        tester.set_input("plugh dir\\");
        tester.set_expected_matches("dir\\bark", "dir\\boxy", "dir\\blip", "dir\\boom");
        tester.run();
    }

    SECTION("non-pathish matches")
    {
        tester.set_input("xyzzy foo/");
        tester.set_expected_matches("foo/bar", "foo/bark", "foo/box");
        tester.run();
    }

    SECTION("pathish readline")
    {
        tester.set_input("plugh dir/\x1b*");
        tester.set_expected_output("plugh dir\\bark dir\\blip dir\\boom dir\\boxy ");
        tester.run();
    }

    SECTION("non-pathish readline")
    {
        tester.set_input("xyzzy foo/\x1b*");
        tester.set_expected_output("xyzzy foo/bar foo/bark foo/box ");
        tester.run();
    }

    setting->set();
}

//------------------------------------------------------------------------------
TEST_CASE("Match type : compound")
{
    fs_fixture fs(matchtype_fs);

    setting* setting = settings::find("match.translate_slashes");
    setting->set("system");

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua.do_string(script, int32(strlen(script)));

    line_editor_tester tester;
    tester.get_editor()->set_generator(lua_generator);

    SECTION("pathish matches")
    {
        tester.set_input("plugh dir/ba");
        tester.set_expected_matches("dir\\bark");
        tester.run();
    }

    SECTION("non-pathish matches")
    {
        tester.set_input("xyzzy foo/ba");
        tester.set_expected_matches("foo/bar", "foo/bark");
        tester.run();
    }

    SECTION("pathish readline")
    {
        tester.set_input("plugh dir\\ba\x1b*");
        tester.set_expected_output("plugh dir\\bark ");
        tester.run();
    }

    SECTION("non-pathish readline")
    {
        tester.set_input("xyzzy foo/ba\x1b*");
        tester.set_expected_output("xyzzy foo/bar foo/bark ");
        tester.run();
    }

    setting->set();
}

//------------------------------------------------------------------------------
TEST_CASE("Match type : lcd")
{
    fs_fixture fs(matchtype_fs);

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua.do_string(script, int32(strlen(script)));

    line_editor_tester tester;
    tester.get_editor()->set_generator(lua_generator);

    rl_bind_keyseq_in_map("\x99", rl_named_function("complete"), emacs_standard_keymap);

    SECTION("pathish readline")
    {
        tester.set_input("plugh dir\\\x99");
        tester.set_expected_output("plugh dir\\b");
        tester.run();
    }

    SECTION("non-pathish readline")
    {
        tester.set_input("xyzzy foo/\x99");
        tester.set_expected_output("xyzzy foo/b");
        tester.run();
    }

    rl_bind_keyseq_in_map("\x99", rl_insert, emacs_standard_keymap);
}

//------------------------------------------------------------------------------
TEST_CASE("Match type : files")
{
    fs_fixture fs(matchtype_fs);

    setting* setting = settings::find("match.translate_slashes");
    setting->set("system");

    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua.do_string(script, int32(strlen(script)));

    line_editor_tester tester;
    tester.get_editor()->set_generator(lua_generator);

    SECTION("pathish readline")
    {
        tester.set_input("plugh dir\\b\x1b*");
        tester.set_expected_output("plugh dir\\bark dir\\blip dir\\boom dir\\boxy ");
        tester.run();
    }

    SECTION("non-pathish readline")
    {
        tester.set_input("xyzzy foo/b\x1b*");
        tester.set_expected_output("xyzzy foo/bar foo/bark foo/box ");
        tester.run();
    }

    setting->set();
}
