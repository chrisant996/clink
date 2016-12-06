// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_tester.h"

#include <core/str.h>
#include <rl/rl_history.h>

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
TEST_CASE("History") {
    const char* history_lines[] = {
        "cmd1 arg1 arg2 arg3 arg4",
        "cmd2 arg1 arg2 arg3 arg4 extra",
        "cmd3 arg1 arg2 arg3 arg4",
    };

    rl_history history;
    for (const char* line : history_lines)
        history.add(line);

    SECTION("Navigation") {
        line_editor_tester tester;

        SECTION("Ctrl-P 1") {
            tester.set_input(CTRL_P);
            tester.set_expected_output(history_lines[2]);
            tester.run();
        }

        SECTION("Ctrl-P 2") {
            tester.set_input(CTRL_P CTRL_P);
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }

        SECTION("Ctrl-P 3") {
            tester.set_input(CTRL_P CTRL_P CTRL_P);
            tester.set_expected_output(history_lines[0]);
            tester.run();
        }

        SECTION("Ctrl-P 4") {
            tester.set_input(CTRL_P CTRL_P CTRL_P CTRL_P);
            tester.set_expected_output(history_lines[0]);
            tester.run();
        }

        SECTION("Ctrl-N 1") {
            tester.set_input("abc" CTRL_P CTRL_N);
            tester.set_expected_output("abc");
            tester.run();
        }

        SECTION("Ctrl-N 2") {
            tester.set_input(CTRL_P CTRL_P CTRL_P CTRL_P CTRL_N);
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }
    }

    SECTION("Search") {
        line_editor_tester tester;

        SECTION("Ctrl-R Ctrl-E") {
            tester.set_input(CTRL_R "extra" CTRL_E);
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }

        SECTION("Ctrl-R Ctrl-A") {
            tester.set_input(CTRL_R "cmd1" CTRL_A);
            tester.set_expected_output(history_lines[0]);
            tester.run();
        }

        SECTION("Ctrl-R <Esc>") {
            tester.set_input(CTRL_R "cmd2" "\x1b");
            tester.set_expected_output(history_lines[1]);
            tester.run();
        }

        SECTION("Ctrl-R <Home>") {
            char* kh;
            kh = tgetstr("kh", nullptr);

            str<> input;
            input << CTRL_R << "cmd2" << kh;

            tester.set_input(input.c_str());
            tester.set_expected_output(history_lines[1]);
            tester.run();

            free(kh);
        }
    }

    SECTION("Expansion") {
        str<> out;

        SECTION("!0") {
            REQUIRE(history.expand("!0", out) == -1);
            REQUIRE(out.empty());
        }

        SECTION("!!") {
            REQUIRE(history.expand("!!", out) == 1);
            REQUIRE(out.equals(history_lines[2]));
        }

        SECTION("!string") {
            REQUIRE(history.expand("!cmd2", out) == 1);
            REQUIRE(out.equals(history_lines[1]));
        }

        SECTION("!1") {
            REQUIRE(history.expand("!1", out) == 1);
            REQUIRE(out.equals(history_lines[0]));
        }

        SECTION("!#") {
            REQUIRE(history.expand("one two !#", out) == 1);
            REQUIRE(out.equals("one two one two "));
        }

        SECTION("!?string") {
            history.add("one two");
            REQUIRE(history.expand("three !?one", out) == 1);
            REQUIRE(out.equals("three one two"));
        }

        SECTION("!$") {
            REQUIRE(history.expand("cmdX !$", out) == 1);
            REQUIRE(out.equals("cmdX arg4"));
        }

        SECTION("!!:$") {
            REQUIRE(history.expand("cmdX !!:$", out) == 1);
            REQUIRE(out.equals("cmdX arg4"));
        }

        SECTION("!!:N-$") {
            REQUIRE(history.expand("cmdX !!:3-$", out) == 1);
            REQUIRE(out.equals("cmdX arg3 arg4"));
        }

        SECTION("!!:N*") {
            REQUIRE(history.expand("cmdX !!:2*", out) == 1);
            REQUIRE(out.equals("cmdX arg2 arg3 arg4"));
        }

        SECTION("!!:N") {
            REQUIRE(history.expand("cmdX !!:2", out) == 1);
            REQUIRE(out.equals("cmdX arg2"));
        }

        SECTION("!!:-N") {
            REQUIRE(history.expand("cmdX !!:-1", out) == 1);
            REQUIRE(out.equals("cmdX cmd3 arg1"));
        }

        SECTION("^X^Y^") {
            REQUIRE(history.expand("^arg1^123^", out) == 1);
            REQUIRE(out.equals("cmd3 123 arg2 arg3 arg4"));
        }

        SECTION("!X:s/Y/Z") {
            REQUIRE(history.expand("!cmd1:s/arg1/123", out) == 1);
            REQUIRE(out.equals("cmd1 123 arg2 arg3 arg4"));
        }

        SECTION("!?X?:") {
            REQUIRE(history.expand("cmdX !?extra?:*", out) == 1);
            REQUIRE(out.equals("cmdX arg1 arg2 arg3 arg4 extra"));
        }
    }
}
