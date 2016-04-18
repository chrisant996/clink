// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"
#include "match_generator_tester.h"

#include <core/str_compare.h>
#include <lib/match_generator.h>

//------------------------------------------------------------------------------
struct file_test
{
    typedef match_generator_tester<file_test> tester;
    operator match_generator& () { return file_match_generator(); }
};

//------------------------------------------------------------------------------
TEST_CASE("File match generator") {
    fs_fixture fs;

    SECTION("File system matches") {
        file_test::tester(
            "",
            "", "case_map-1", "case_map_2", "dir1\\", "dir2\\",
            "file1", "file2", nullptr
        );
    }

    SECTION("Single file") {
        file_test::tester("file1", "file1", nullptr);
    }

    SECTION("Single dir") {
        file_test::tester("dir1", "dir1\\", nullptr);
    }

    SECTION("Dir slash flip") {
        file_test::tester(
            "dir1/",
            "dir1\\", "dir1\\only", "dir1\\file1", "dir1\\file2", nullptr
        );
    }

    SECTION("Path slash flip") {
        file_test::tester("dir1/on", "dir1\\only", nullptr);
    }

    SECTION("Case mapping matches") {
        str_compare_scope _(str_compare_scope::relaxed);

        file_test::tester(
            "case-m",
            "case_map", "case_map-1", "case_map_2", nullptr
        );
    }

    /*
    SECTION("Case mapping complex") {
        file_test::tester(
            "cmd case_map-",
            expect(result, "cmd case_map-", nullptr
        );
        REQUIRE(result.get_match_count() == 1);
    }
    */
}
