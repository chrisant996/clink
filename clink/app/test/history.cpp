// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "env_fixture.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/settings.h>
#include <core/str.h>
#include <history/history_db.h>
#include <utils/app_context.h>

#include <initializer_list>

//------------------------------------------------------------------------------
extern "C" {
char* tgetstr(char*, char**);
}

//------------------------------------------------------------------------------
#define CTRL_A "\x01"
#define CTRL_E "\x05"
#define CTRL_N "\x0e"
#define CTRL_P "\x10"
#define CTRL_R "\x12"

//------------------------------------------------------------------------------
struct test_history_db
    : public history_db
{
    test_history_db()
    {
        initialise();
    }
};

//------------------------------------------------------------------------------
int count_files()
{
    globber file_iter("*");
    file_iter.hidden(true);

    int file_count = 0;
    for (; file_iter.next(str<>()); ++file_count);

    return file_count;
}

//------------------------------------------------------------------------------
template <typename T>
void expect_files(const std::initializer_list<T>& names, bool exclusive=true)
{
    for (const char* name : names)
    {
        REQUIRE(os::get_path_type(name) != os::path_type_invalid, [&] () {
            printf("Missing '%s'\n", name);
        });
    }

	if (exclusive)
		REQUIRE(count_files() == names.size());
}



//------------------------------------------------------------------------------
TEST_CASE("history db")
{
    const char* line_set0[] = {
        "line_set0_0", "line_set0_1", "line_set0_2", "line_set0_3",
        "line_set0_4", "line_set0_5", "line_set0_6", "line_set0_7",
    };

    const char* line_set1[] = {
        "line_set1_0", "line_set1_1", "line_set1_2", "line_set1_3",
    };

    const char* master_path = "clink_history";
    const char* session_path = "clink_history_493";
    const char* alive_path = "clink_history_493~";

    // Start with an empty state dir.
    const char* empty_fs[] = { nullptr };
    fs_fixture fs(empty_fs);
    REQUIRE(count_files() == 0);

    // This sets the state id to something explicit.
    static const char* env_desc[] = {
        "=clink.id", "493",
        nullptr
    };
    env_fixture env(env_desc);

    app_context::desc context_desc;
    context_desc.inherit_id = true;
    str_base(context_desc.state_dir).copy(fs.get_root());
    app_context context(context_desc);

    SECTION("Alive file")
    {
        // Shared
        settings::find("history.shared")->set("true");
        {
            test_history_db history;
            expect_files({master_path, alive_path});
        }
        expect_files({master_path});

        // Sessioned
        settings::find("history.shared")->set("false");
        {
            test_history_db history;
            expect_files({master_path, session_path, alive_path});
        }
        expect_files({master_path});
    }

    SECTION("Shared")
    {
        settings::find("history.shared")->set("true");
        settings::find("history.dupe_mode")->set("add");

        int line_bytes = 0;

        // Write a lot of lines, check it only goes to main file.
        {
            test_history_db history;
            REQUIRE(count_files() == 2);

            while (line_bytes < 64 * 1024)
            {
                for (const char* line : line_set0)
                {
                    REQUIRE(history.add(line));
                    line_bytes += int(strlen(line)) + 1; // +1 for \n
                }
            }

            REQUIRE(os::get_file_size(master_path) == line_bytes);
        }

        // Clear the history.
        {
            test_history_db history;
            history.clear();
            REQUIRE(os::get_file_size(master_path) == 0);
        }

        REQUIRE(count_files() == 1);
    }

    SECTION("Sessioned")
    {
        settings::find("history.shared")->set("false");
        settings::find("history.dupe_mode")->set("add");

        int line_bytes = 0;
        {
            test_history_db history;
            REQUIRE(count_files() == 3);

            REQUIRE(history.add(line_set0[0]));
            line_bytes += int(strlen(line_set0[0])) + 1;

            REQUIRE(count_files() == 3);
            REQUIRE(os::get_file_size(session_path) == line_bytes);
            REQUIRE(os::get_file_size(master_path) == 0);
        }

        REQUIRE(count_files() == 1);
        REQUIRE(os::get_file_size(master_path) == line_bytes);
    }

    SECTION("line iter")
    {
        str<> lines;
        for (char i = 0; i < 8; ++i)
        {
            char c = 'a' + (i * 2);
            char line[] = { c, c + 1, '\n', 0 };
            lines << line;
        }

        FILE* out = fopen(session_path, "wb");
        fwrite(lines.c_str(), lines.length(), 1, out);
        fclose(out);

        test_history_db history;

        char buffer[256];
        str_iter line;
        int j = 0;
        for (int i = 0; i < sizeof_array(buffer); ++i)
        {
            history_db::iter iter = history.read_lines(buffer, i);
            char c = 'a';
            while (iter.next(line))
            {
                int line_length = line.length();
                REQUIRE(line_length == 1 || line_length == 2);

                REQUIRE(line.get_pointer()[0] == c++);
                if (line_length == 2)
                    REQUIRE(line.get_pointer()[1] == c++);
            }
        }
    }
}

//------------------------------------------------------------------------------
TEST_CASE("history rl")
{
    // Start with an empty state dir.
    const char* empty_fs[] = { nullptr };
    fs_fixture fs(empty_fs);

    // This sets the state id to something explicit.
    static const char* env_desc[] = {
        "=clink.id", "493",
        nullptr
    };
    env_fixture env(env_desc);

    app_context::desc context_desc;
    context_desc.inherit_id = true;
    str_base(context_desc.state_dir).copy(fs.get_root());
    app_context context(context_desc);

    // Fill the history. reload() call will fill Readline.
    static const char* history_lines[] = {
        "cmd1 arg1 arg2 arg3 arg4",
        "cmd2 arg1 arg2 arg3 arg4 extra",
        "cmd3 arg1 arg2 arg3 arg4",
    };

    test_history_db history;
    for (const char* line : history_lines)
        history.add(line);
    history.load_rl_history();

    // Here be the tests.
    SECTION("Navigation")
    {
        line_editor_tester tester;

        SECTION("Ctrl-P 1")
        {
            tester.set_input(CTRL_P);
            tester.set_expected_output(history_lines[2]);
            tester.run();
        }

        SECTION("Ctrl-P 2")
        {
            tester.set_input(CTRL_P CTRL_P);
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }

        SECTION("Ctrl-P 3")
        {
            tester.set_input(CTRL_P CTRL_P CTRL_P);
            tester.set_expected_output(history_lines[0]);
            tester.run();
        }

        SECTION("Ctrl-P 4")
        {
            tester.set_input(CTRL_P CTRL_P CTRL_P CTRL_P);
            tester.set_expected_output(history_lines[0]);
            tester.run();
        }

        SECTION("Ctrl-N 1")
        {
            tester.set_input("abc" CTRL_P CTRL_N);
            tester.set_expected_output("abc");
            tester.run();
        }

        SECTION("Ctrl-N 2")
        {
            tester.set_input(CTRL_P CTRL_P CTRL_P CTRL_P CTRL_N);
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }
    }

    SECTION("Search")
    {
        line_editor_tester tester;

        SECTION("Ctrl-R Ctrl-E")
        {
            tester.set_input(CTRL_R "extra" CTRL_E);
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }

        SECTION("Ctrl-R Ctrl-A")
        {
            tester.set_input(CTRL_R "cmd1" CTRL_A);
            tester.set_expected_output(history_lines[0]);
            tester.run();
        }

        SECTION("Ctrl-R <Esc>")
        {
            tester.set_input(CTRL_R "cmd2" "\x1b");
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }

        SECTION("Ctrl-R <Home>")
        {
            char kh_cap[] = "kh";
            char* kh = tgetstr(kh_cap, nullptr);

            str<> input;
            input << CTRL_R << "cmd2" << kh;

            tester.set_input(input.c_str());
            tester.set_expected_output(history_lines[1]);
            tester.run();

            free(kh);
        }
    }

    SECTION("Expansion")
    {
        str<> out;

        SECTION("!0")
        {
            REQUIRE(history.expand("!0", out) == history_db::expand_error);
            REQUIRE(out.empty());
        }

        SECTION("!!")
        {
            REQUIRE(history.expand("!!", out) == history_db::expand_ok);
            REQUIRE(out.equals(history_lines[2]));
        }

        SECTION("!string")
        {
            REQUIRE(history.expand("!cmd2", out) == history_db::expand_ok);
            REQUIRE(out.equals(history_lines[1]));
        }

        SECTION("!1")
        {
            REQUIRE(history.expand("!1", out) == history_db::expand_ok);
            REQUIRE(out.equals(history_lines[0]));
        }

        SECTION("!#")
        {
            REQUIRE(history.expand("one two !#", out) == history_db::expand_ok);
            REQUIRE(out.equals("one two one two "));
        }

        SECTION("!?string")
        {
            history.add("one two");
            history.load_rl_history();
            REQUIRE(history.expand("three !?one", out) == history_db::expand_ok);
            REQUIRE(out.equals("three one two"));
        }

        SECTION("!$")
        {
            REQUIRE(history.expand("cmdX !$", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX arg4"));
        }

        SECTION("!!:$")
        {
            REQUIRE(history.expand("cmdX !!:$", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX arg4"));
        }

        SECTION("!!:N-$")
        {
            REQUIRE(history.expand("cmdX !!:3-$", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX arg3 arg4"));
        }

        SECTION("!!:N*")
        {
            REQUIRE(history.expand("cmdX !!:2*", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX arg2 arg3 arg4"));
        }

        SECTION("!!:N")
        {
            REQUIRE(history.expand("cmdX !!:2", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX arg2"));
        }

        SECTION("!!:-N")
        {
            REQUIRE(history.expand("cmdX !!:-1", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX cmd3 arg1"));
        }

        SECTION("^X^Y^")
        {
            REQUIRE(history.expand("^arg1^123^", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmd3 123 arg2 arg3 arg4"));
        }

        SECTION("!X:s/Y/Z")
        {
            REQUIRE(history.expand("!cmd1:s/arg1/123", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmd1 123 arg2 arg3 arg4"));
        }

        SECTION("!?X?:")
        {
            REQUIRE(history.expand("cmdX !?extra?:*", out) == history_db::expand_ok);
            REQUIRE(out.equals("cmdX arg1 arg2 arg3 arg4 extra"));
        }
    }
}
