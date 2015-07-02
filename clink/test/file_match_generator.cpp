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
#include <core/str.h>
#include <file_match_generator.h>
#include <line_state.h>
#include <stdarg.h>

//------------------------------------------------------------------------------
static match_result run_test(const char* line)
{
    str<> command = line;
    int start = command.last_of(' ') + 1;
    int end = command.length();
    int cursor = end;
    const char* word = command.c_str() + start;

    line_state state = { word, command.c_str(), start, end, cursor };
    return file_match_generator().generate(state);
}

//------------------------------------------------------------------------------
static void expect(const match_result& result, ...)
{
    va_list arg;
    va_start(arg, result);

    int match_count = 0;
    while (const char* match = va_arg(arg, const char*))
    {
        bool ok = false;
        for (int i = 0, n = result.get_match_count(); i < n; ++i)
            if (ok = (strcmp(match, result.get_match(i)) == 0))
                break;

        if (ok)
            ++match_count;
    }

    REQUIRE(result.get_match_count() == match_count);
}

//------------------------------------------------------------------------------
TEST_CASE("File match generator") {
    SECTION("File system matches") {
        auto result = run_test("cmd ");
        expect(result, "", "case_map-1", "case_map_2", "dir1\\", "dir2\\",
            "file1", "file2", nullptr);
    }

    SECTION("Single file") {
        auto result = run_test("cmd file1");
        expect(result, "file1", "file1", nullptr);
    }

    SECTION("Single dir") {
        auto result = run_test("cmd dir1");
        expect(result, "dir1\\", "dir1\\", nullptr);
    }

    SECTION("Dir slash flip") {
        auto result = run_test("cmd dir1/");
        expect(result, "dir1/", "dir1\\only", "dir1\\file1", "dir1\\file2", nullptr);
    }

    SECTION("Path slash flip") {
        auto result = run_test("cmd dir1/on");
        expect(result, "dir1\\only", "dir1\\only", nullptr);
    }

    SECTION("Case mapping matches") {
        auto result = run_test("cmd case-m");
        expect(result, "case-map", "case_map-1", "case_map_2", nullptr);
    }

    /*
    SECTION("Case mapping complex") {
        auto result = run_test("cmd case_map-");
        expect(result, "cmd case_map-");
        REQUIRE(result.get_match_count() == 1);
    }
    */
}
