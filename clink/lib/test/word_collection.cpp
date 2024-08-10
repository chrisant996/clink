// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <lib/cmd_tokenisers.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>
#include <readline/readline.h>
#include <memory>

//------------------------------------------------------------------------------
TEST_CASE("Word collection")
{
    lua_state lua;
    lua_match_generator lua_generator(lua);
    lua_load_script(lua, app, dir);

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;

    line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
    desc.command_tokeniser = &command_tokeniser;
    desc.word_tokeniser = &word_tokeniser;
    line_editor_tester tester(desc, nullptr, nullptr);
    tester.get_editor()->set_generator(lua_generator);

    tester.set_input(">ab^ cd jkl mno ");
    tester.set_expected_words("ab^ cd", "jkl", "mno", "");
    tester.run();

    tester.set_input("^\"^\" jkl mno ");
    tester.set_expected_words("^\"^\"", "jkl", "mno", "");
    tester.run();

    tester.set_input("\"abc\" ");
    tester.set_expected_words("abc", "");
    tester.run();

    tester.set_input("\"ab\"cd jkl mno ");
    tester.set_expected_words("ab\"cd", "jkl", "mno", "");
    tester.run();

    tester.set_input("abc\" jkl mno ");
    tester.set_expected_words("");
    tester.run();

    tester.set_input("abc\" jkl mno\" ");
    tester.set_expected_words("abc\" jkl mno\"", "");
    tester.run();

    tester.set_input("foo ^ bar ");
    tester.set_expected_words("foo", "bar", "");
    tester.run();

    // The list of expected words is compared against the line_state used by
    // completion, so the end word is empty here because the last quote makes
    // the trailing space part of the end word `calc" `.
    // This uses `x` instead of `&` because the expected words processing in
    // the line_editor_tester applies to the line_state for completion, and
    // `&` would start a new line_state.
    tester.set_input("abc \"hi^\" x calc\" ");
    tester.set_expected_words("abc", "hi^", "x", "");
    tester.run();
}
