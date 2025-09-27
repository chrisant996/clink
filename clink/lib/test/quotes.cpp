// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/str_compare.h>
#include <core/settings.h>
#include <lib/match_generator.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>

//------------------------------------------------------------------------------
TEST_CASE("Quoting")
{
    static const char* space_fs[] = {
        "pre_nospace",
        "pre_space 1",
        "pre_space_space 2",
        "single space",
        "dir/space 1",
        "dir/space 2",
        "dir/space_3",
        "dir/single space",
        nullptr,
    };

    fs_fixture fs(space_fs);

    setting* setting = settings::find("match.translate_slashes");
    setting->set("system");

    lua_state lua;
    lua_match_generator lua_generator(lua);

    SECTION("Double quotes")
    {
        line_editor_tester tester;

        line_editor* editor = tester.get_editor();
        editor->set_generator(lua_generator);

        SECTION("None")
        {
            tester.set_input("pr" DO_COMPLETE);
            tester.set_expected_output("pre_");
            tester.run();
        }

        SECTION("Close existing")
        {
            tester.set_input("\"singl" DO_COMPLETE);
            tester.set_expected_output("\"single space\" ");
            tester.run();
        }

        SECTION("End-of-word")
        {
            tester.set_input("\"single space\"" DO_COMPLETE);
            tester.set_expected_output("\"single space\" ");
            tester.run();
        }

        SECTION("Surround")
        {
            tester.set_input("sing" DO_COMPLETE);
            tester.set_expected_output("\"single space\" ");
            tester.run();
        }

        SECTION("Prefix")
        {
            tester.set_input("pre_s" DO_COMPLETE);
            tester.set_expected_output("\"pre_space");
            tester.run();
        }

        SECTION("Prefix (case mapped)")
        {
            str_compare_scope _(str_compare_scope::relaxed, false);
            tester.set_input("pre-s" DO_COMPLETE);
            tester.set_expected_output("\"pre_space");
            tester.run();
        }

        SECTION("Dir (close existing)")
        {
            tester.set_input("\"dir/sing" DO_COMPLETE);
            tester.set_expected_output("\"dir\\single space\" ");
            tester.run();
        }

        SECTION("Dir (surround)")
        {
            tester.set_input("dir/sing" DO_COMPLETE);
            tester.set_expected_output("\"dir\\single space\" ");
            tester.run();
        }

        SECTION("Dir (prefix)")
        {
            tester.set_input("dir\\spac" DO_COMPLETE);
            tester.set_expected_output("\"dir\\space");
            tester.run();
        }
    }

#if 0
    SECTION("Matched pair")
    {
        line_editor::desc desc;
        desc.quote_pair = "()";
        line_editor_tester tester(desc);

        line_editor* editor = tester.get_editor();
        editor->set_generator(lua_generator);

        SECTION("None")
        {
            tester.set_input("pr" DO_COMPLETE);
            tester.set_expected_output("pre_");
            tester.run();
        }

        SECTION("Close existing")
        {
            tester.set_input("(singl" DO_COMPLETE);
            tester.set_expected_output("(single space) ");
            tester.run();
        }

        SECTION("End-of-word")
        {
            tester.set_input("(single space)" DO_COMPLETE);
            tester.set_expected_output("(single space) ");
            tester.run();
        }

        SECTION("Surround")
        {
            tester.set_input("sing" DO_COMPLETE);
            tester.set_expected_output("(single space) ");
            tester.run();
        }

        SECTION("Prefix")
        {
            tester.set_input("pre_s" DO_COMPLETE);
            tester.set_expected_output("(pre_space");
            tester.run();
        }
    }
#endif

#if 0
    SECTION("No quote pair")
    {
        line_editor::desc desc;
        desc.quote_pair = nullptr;
        line_editor_tester tester(desc);

        line_editor* editor = tester.get_editor();
        editor->set_generator(lua_generator);

        SECTION("None")
        {
            tester.set_input("pr" DO_COMPLETE);
            tester.set_expected_output("pre_");
            tester.run();
        }

        SECTION("Close existing")
        {
            tester.set_input("singl" DO_COMPLETE);
            tester.set_expected_output("single space ");
            tester.run();
        }

        SECTION("Surround")
        {
            tester.set_input("sing" DO_COMPLETE);
            tester.set_expected_output("single space ");
            tester.run();
        }

        SECTION("Prefix")
        {
            tester.set_input("pre_s" DO_COMPLETE);
            tester.set_expected_output("pre_space");
            tester.run();
        }
    }
#endif

    setting->set();
}
