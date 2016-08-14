// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "line_editor_tester.h"

#include <core/str_compare.h>
#include <lib/match_generator.h>

//------------------------------------------------------------------------------
TEST_CASE("File match generator") {
    fs_fixture fs;
    line_editor_tester tester;
    tester.get_editor()->add_generator(file_match_generator());

    SECTION("File system matches") {
        tester.set_input("");
        tester.set_expected_matches("case_map-1", "case_map_2", "dir1\\",
            "dir2\\", "file1", "file2");
        tester.run();
    }

    SECTION("Single file") {
        tester.set_input("file1");
        tester.set_expected_matches("file1");
        tester.run();
    }

    SECTION("Single dir") {
        tester.set_input("dir1");
        tester.set_expected_matches("dir1\\");
        tester.run();
    }

    SECTION("Dir slash flip") {
        tester.set_input("dir1/");
        tester.set_expected_matches("only", "file1", "file2");
        //tester.set_expected_output("dir1\\"); // MODE4
        tester.run();
    }

    SECTION("Path slash flip") {
        tester.set_input("dir1/on\t");
        tester.set_expected_output("dir1\\only ");
        tester.run();
    }

    SECTION("Case mapping matches") {
        str_compare_scope _(str_compare_scope::relaxed);

        tester.set_input("case-m");
        tester.set_expected_matches("case_map-1", "case_map_2");
        //tester.set_expected_output("case_map"); // MODE4
        tester.run();
    }
}
