// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "env_fixture.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/os.h>
#include <core/path.h>
#include <core/str_compare.h>
#include <core/settings.h>
#include <lib/match_generator.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_state.h>

#include <readline/readline.h>

//------------------------------------------------------------------------------
static const char* dyn_section(const char* section, const char* mode)
{
    static char buf[128];
    sprintf_s(buf, "%s (%s)", section, mode);
    return buf;
}

//------------------------------------------------------------------------------
TEST_CASE("File match generator")
{
    fs_fixture fs;

    static const char* env_inputrc[] = {
        "clink_inputrc", "dummy_to_use_defaults",
        nullptr
    };
    env_fixture env(env_inputrc);

    setting* setting = settings::find("match.translate_slashes");
    setting->set("system");

    lua_state lua;
    lua_match_generator lua_generator(lua);

    static const char* inputrc_vars[] = {
        "set mark-directories on",      "mark-dir=ON",
        "set mark-directories off",     "mark-dir=off",
        nullptr
    };
    for (int32 v = 0; inputrc_vars[v]; v += 2)
    {
        const char* mode = inputrc_vars[v + 1];
        str<> setvar(inputrc_vars[v]);
        REQUIRE(rl_parse_and_bind(setvar.data()) == 0);

        line_editor_tester tester;
        tester.get_editor()->set_generator(lua_generator);

        SECTION(dyn_section("File system matches", mode))
        {
            tester.set_input("");
            tester.set_expected_matches("case_map-1", "case_map_2", "dir1\\",
                "dir2\\", "file1", "file2");
            tester.run();
        }

        SECTION(dyn_section("Single file", mode))
        {
            tester.set_input("file1");
            tester.set_expected_matches("file1");
            tester.run();
        }

        SECTION(dyn_section("Single dir", mode))
        {
            tester.set_input("dir1");
            tester.set_expected_matches("dir1\\");
            tester.run();
        }

        SECTION(dyn_section("Single dir complete", mode))
        {
            tester.set_input("dir1" DO_COMPLETE);
            if (v == 0)
            {
                tester.set_expected_output("dir1\\");
                tester.set_expected_matches("dir1\\only", "dir1\\file1", "dir1\\file2");
            }
            else
            {
                tester.set_expected_output("dir1");
                tester.set_expected_matches("dir1\\");
            }
            tester.run();
        }
        SECTION(dyn_section("Dir slash flip", mode))
        {
            tester.set_input("dir1/" DO_COMPLETE);
            tester.set_expected_matches("dir1\\only", "dir1\\file1", "dir1\\file2");
            tester.set_expected_output("dir1\\");
            tester.run();
        }

        SECTION(dyn_section("Path slash flip", mode))
        {
            tester.set_input("dir1/on" DO_COMPLETE);
            tester.set_expected_output("dir1\\only ");
            tester.run();
        }

        SECTION(dyn_section("Case mapping matches (caseless)", mode))
        {
            str_compare_scope _(str_compare_scope::caseless, false);

            tester.set_input("case_m" DO_COMPLETE);
            tester.set_expected_matches("case_map-1", "case_map_2");
            tester.set_expected_output("case_map");
            tester.run();
        }

        SECTION(dyn_section("Case mapping matches (relaxed)", mode))
        {
            str_compare_scope _(str_compare_scope::relaxed, false);

            tester.set_input("case-m" DO_COMPLETE);
            tester.set_expected_matches("case_map-1", "case_map_2");
            tester.set_expected_output("case_map_");
            tester.run();
        }

        SECTION(dyn_section("Relative", mode))
        {
            REQUIRE(os::set_current_dir("dir1"));

            tester.set_input("../dir1/on" DO_COMPLETE);
            tester.set_expected_output("..\\dir1\\only ");
            tester.run();

            REQUIRE(os::set_current_dir(".."));
        }

        SECTION(dyn_section("cmd-style drive relative", mode))
        {
            str<280> fs_path;
            path::join(fs.get_root(), "dir1", fs_path);

            const char* env_vars[] = { "=m:", fs_path.c_str(), nullptr };

            env_fixture env(env_vars);
            tester.set_input("m:" DO_COMPLETE);
            tester.set_expected_output("m:");
            tester.set_expected_matches("m:only", "m:file1", "m:file2");
            tester.run();
        }

        SECTION(dyn_section("redundant separators", mode))
        {
            tester.set_input("dir1\\\\\\" DO_COMPLETE);
            tester.set_expected_matches("dir1\\only", "dir1\\file1", "dir1\\file2");
            tester.set_expected_output("dir1\\");
            tester.run();

            tester.set_input("dir1\\\\\\f" DO_COMPLETE);
            tester.set_expected_matches("dir1\\file1", "dir1\\file2");
            tester.set_expected_output("dir1\\file");
            tester.run();
        }
    }

    setting->set();
}
