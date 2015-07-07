/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "catch.hpp"
#include "scoped_test_fs.h"
#include "match_generator_tester.h"
#include <file_match_generator.h>

//------------------------------------------------------------------------------
TEST_CASE("File match generator") {
	scoped_test_fs fs;

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
