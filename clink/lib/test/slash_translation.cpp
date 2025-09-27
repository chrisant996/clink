// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

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
TEST_CASE("Slash translation")
{
    static const char* dir_fs[] = {
        "one_dir/leaf",
        "two_dir/leaf",
        "three_dir/leaf",
        "nest_1/nest_2/leaf",
        "nest_1/nest_2/nest_3a/leaf",
        "nest_1/nest_2/nest_3b/leaf",
        "one_file",
        "two_file",
        "three_file",
        "four_file",
        nullptr,
    };

    fs_fixture fs(dir_fs);

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;

    // Pass 0 is with simple tokenisers.
    // Pass 1 is with cmd_* tokenisers.
    for (uint32 ii = 0; ii < 2; ++ii)
    {
        const bool cmd_pass = (ii == 1);

        lua_state lua;
        lua_match_generator lua_generator(lua);
        lua_load_script(lua, app, dir);

        std::unique_ptr<line_editor_tester> _tester;
        if (cmd_pass)
        {
            line_editor::desc desc(nullptr, nullptr, nullptr, nullptr);
            desc.command_tokeniser = &command_tokeniser;
            desc.word_tokeniser = &word_tokeniser;
            _tester = std::make_unique<line_editor_tester>(desc, nullptr, nullptr);
        }
        else
        {
            // To use simple tokenisers the generic ctor must be used.
            _tester = std::make_unique<line_editor_tester>();
        }

        line_editor_tester& tester = *_tester.get();
        tester.get_editor()->set_generator(lua_generator);

        setting* setting = settings::find("match.translate_slashes");

        SECTION("translate_slashes == off")
        {
            setting->set("off");

            tester.set_input("foo nest_1/nest_2\\");
            tester.set_expected_matches("nest_1/nest_2\\nest_3a\\", "nest_1/nest_2\\nest_3b\\", "nest_1/nest_2\\leaf");
            tester.run();

            tester.set_input("foo nest_1/nest_2\\nest_3a/");
            tester.set_expected_matches("nest_1/nest_2\\nest_3a/leaf");
            tester.run();

            tester.set_input("foo nest_1\\nest_2/nest_3a\\");
            tester.set_expected_matches("nest_1\\nest_2/nest_3a\\leaf");
            tester.run();

            tester.set_input("./o");
            if (cmd_pass)
                tester.set_expected_words(".", "/");
            else
                tester.set_expected_words("./");
            tester.run();
        }

        SECTION("translate_slashes == system")
        {
            setting->set("system");

            tester.set_input("foo nest_1/nest_2/");
            tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\", "nest_1\\nest_2\\nest_3b\\", "nest_1\\nest_2\\leaf");
            tester.run();

            tester.set_input("foo nest_1/nest_2/nest_3a/");
            tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\leaf");
            tester.run();

            tester.set_input("./o");
            tester.set_expected_words("./");
            tester.run();
        }

        SECTION("translate_slashes == slash")
        {
            setting->set("slash");

            tester.set_input("foo nest_1\\nest_2\\");
            tester.set_expected_matches("nest_1/nest_2/nest_3a/", "nest_1/nest_2/nest_3b/", "nest_1/nest_2/leaf");
            tester.run();

            tester.set_input("foo nest_1\\nest_2\\nest_3a\\");
            tester.set_expected_matches("nest_1/nest_2/nest_3a/leaf");
            tester.run();

            tester.set_input("./o");
            if (cmd_pass)
                tester.set_expected_words(".", "/");
            else
                tester.set_expected_words("./");
            tester.run();
        }

        SECTION("translate_slashes == backslash")
        {
            setting->set("backslash");

            tester.set_input("foo nest_1/nest_2/");
            tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\", "nest_1\\nest_2\\nest_3b\\", "nest_1\\nest_2\\leaf");
            tester.run();

            tester.set_input("foo nest_1/nest_2/nest_3a/");
            tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\leaf");
            tester.run();

            tester.set_input("./o");
            tester.set_expected_words("./");
            tester.run();
        }

        SECTION("translate_slashes == auto")
        {
            setting->set("auto");

            tester.set_input("foo nest_1/nest_2\\");
            tester.set_expected_matches("nest_1/nest_2/nest_3a/", "nest_1/nest_2/nest_3b/", "nest_1/nest_2/leaf");
            tester.run();

            tester.set_input("foo nest_1/nest_2\\nest_3a\\");
            tester.set_expected_matches("nest_1/nest_2/nest_3a/leaf");
            tester.run();

            tester.set_input("foo nest_1\\nest_2/");
            tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\", "nest_1\\nest_2\\nest_3b\\", "nest_1\\nest_2\\leaf");
            tester.run();

            tester.set_input("foo nest_1\\nest_2/nest_3a/");
            tester.set_expected_matches("nest_1\\nest_2\\nest_3a\\leaf");
            tester.run();

            tester.set_input("./o");
            if (cmd_pass)
                tester.set_expected_words(".", "/");
            else
                tester.set_expected_words("./");
            tester.run();
        }

        setting->set();
    }
}
