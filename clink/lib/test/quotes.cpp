// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/str_compare.h>
#include <lib/match_generator.h>

//------------------------------------------------------------------------------
TEST_CASE("Quoting") {
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

    editor_module* module = tab_completer_create();

    SECTION("Double quotes") {
        line_editor_tester tester;

        line_editor* editor = tester.get_editor();
        editor->add_module(*module);
        editor->add_generator(file_match_generator());

        SECTION("None") {
            tester.set_input("pr\t");
            tester.set_expected_output("pre_");
            tester.run();
        }

        SECTION("Close exisiting") {
            tester.set_input("\"singl\t");
            tester.set_expected_output("\"single space\" ");
            tester.run();
        }

        SECTION("Surround") {
            tester.set_input("sing\t");
            tester.set_expected_output("\"single space\" ");
            tester.run();
        }

        SECTION("Prefix") {
            tester.set_input("pre_s\t");
            tester.set_expected_output("\"pre_space");
            tester.run();
        }

        SECTION("Prefix (case mapped)") {
            str_compare_scope _(str_compare_scope::relaxed);
            tester.set_input("pre-s\t");
            tester.set_expected_output("\"pre_space");
            tester.run();
        }

        SECTION("Dir (close exisiting)") {
            tester.set_input("\"dir/sing\t");
            tester.set_expected_output("\"dir\\single space\" ");
            tester.run();
        }

        SECTION("Dir (surround)") {
            tester.set_input("dir/sing\t");
            tester.set_expected_output("\"dir\\single space\" ");
            tester.run();
        }

        SECTION("Dir (prefix)") {
            tester.set_input("dir\\spac\t");
            tester.set_expected_output("\"dir\\space");
            tester.run();
        }
    }

    SECTION("Matched pair") {
        line_editor::desc desc;
        desc.quote_pair = "()";
        line_editor_tester tester(desc);

        line_editor* editor = tester.get_editor();
        editor->add_module(*module);
        editor->add_generator(file_match_generator());

        SECTION("None") {
            tester.set_input("pr\t");
            tester.set_expected_output("pre_");
            tester.run();
        }

        SECTION("Close exisiting") {
            tester.set_input("(singl\t");
            tester.set_expected_output("(single space) ");
            tester.run();
        }

        SECTION("Surround") {
            tester.set_input("sing\t");
            tester.set_expected_output("(single space) ");
            tester.run();
        }

        SECTION("Prefix") {
            tester.set_input("pre_s\t");
            tester.set_expected_output("(pre_space");
            tester.run();
        }
    }

    SECTION("No quote pair") {
        line_editor::desc desc;
        desc.quote_pair = nullptr;
        line_editor_tester tester(desc);

        line_editor* editor = tester.get_editor();
        editor->add_module(*module);
        editor->add_generator(file_match_generator());

        SECTION("None") {
            tester.set_input("pr\t");
            tester.set_expected_output("pre_");
            tester.run();
        }

        SECTION("Close exisiting") {
            tester.set_input("singl\t");
            tester.set_expected_output("single space ");
            tester.run();
        }

        SECTION("Surround") {
            tester.set_input("sing\t");
            tester.set_expected_output("single space ");
            tester.run();
        }

        SECTION("Prefix") {
            tester.set_input("pre_s\t");
            tester.set_expected_output("pre_space");
            tester.run();
        }
    }

    tab_completer_destroy(module);
}
