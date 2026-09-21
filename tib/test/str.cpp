// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include <string.h>

using tib::cstring;
using tib::str_transform;
using tib::transform_mode;

static void check_transform(const char* input, transform_mode mode, const char* expected)
{
    cstring output;
    REQUIRE(str_transform(input, tib::c_auto_length, output, mode));
    REQUIRE(!strcmp(output.c_str(), expected), [&](){
        printf("actual;   '%s'\nexpected; '%s'\n", output.c_str(), expected);
    });
    REQUIRE(output.length() == strlen(expected));
}

static void check_in_place(const char* input, size_t offset, size_t length,
                           transform_mode mode, const char* expected)
{
    cstring text(input);
    const char* const range = text.c_str() + offset;

    REQUIRE(str_transform(range, length, text, mode));
    REQUIRE(!strcmp(text.c_str(), expected));
    REQUIRE(text.length() == strlen(expected));
}

TEST_CASE("String transform")
{
    SECTION("Lower case ASCII")
    {
        check_transform("Hello, WORLD! 123", transform_mode::lower,
                        "hello, world! 123");
        check_transform("already lower", transform_mode::lower,
                        "already lower");
    }

    SECTION("Upper case ASCII")
    {
        check_transform("Hello, world! 123", transform_mode::upper,
                        "HELLO, WORLD! 123");
        check_transform("ALREADY UPPER", transform_mode::upper,
                        "ALREADY UPPER");
    }

    SECTION("Title case ASCII")
    {
        check_transform("hello WORLD", transform_mode::title,
                        "Hello World");
        check_transform("one\ttWO\nTHREE", transform_mode::title,
                        "One\tTwo\nThree");
        check_transform("123 abc mixed CASE", transform_mode::title,
                        "123 Abc Mixed Case");
    }

    SECTION("In-place range at beginning")
    {
        check_in_place("Mixed CASE discarded", 0, 10, transform_mode::lower,
                       "mixed case");
    }

    SECTION("In-place range in middle")
    {
        check_in_place("discard mIxEd caSE discard", 8, 10,
                       transform_mode::title, "Mixed Case");
    }

    SECTION("In-place range at end")
    {
        check_in_place("discard mixed Case", 8, 10, transform_mode::upper,
                       "MIXED CASE");
    }
}
