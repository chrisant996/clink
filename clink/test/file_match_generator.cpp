// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "catch.hpp"
#include "fs_fixture.h"
#include "match_generator_tester.h"

#include <file_match_generator.h>

//------------------------------------------------------------------------------
TEST_CASE("File match generator") {
	fs_fixture fs;

    SECTION("File system matches") {
        match_generator_tester<file_match_generator>(
            "cmd ",
            "", "case_map-1", "case_map_2", "dir1\\", "dir2\\",
            "file1", "file2", nullptr
        );
    }

    SECTION("Single file") {
        match_generator_tester<file_match_generator>(
            "cmd file1",
            "file1", "file1", nullptr
        );
    }

    SECTION("Single dir") {
        match_generator_tester<file_match_generator>(
            "cmd dir1",
            "dir1\\", "dir1\\", nullptr
        );
    }

    SECTION("Dir slash flip") {
        match_generator_tester<file_match_generator>(
            "cmd dir1/",
            "dir1\\", "dir1\\only", "dir1\\file1", "dir1\\file2", nullptr
        );
    }

    SECTION("Path slash flip") {
        match_generator_tester<file_match_generator>(
            "cmd dir1/on",
            "dir1\\only", "dir1\\only", nullptr
        );
    }

    SECTION("Case mapping matches") {
        match_generator_tester<file_match_generator>(
            "cmd case-m",
            "case-map", "case_map-1", "case_map_2", nullptr
        );
    }

    /*
    SECTION("Case mapping complex") {
        match_generator_tester<file_match_generator>(
            "cmd case_map-",
            expect(result, "cmd case_map-", nullptr
        );
        REQUIRE(result.get_match_count() == 1);
    }
    */
}
