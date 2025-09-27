// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <core\os.h>
#include <match_colors.h>

//------------------------------------------------------------------------------
static bool test_color(const str_base& s, const char* test)
{
    const char* color = s.c_str();
    if (!color)
        return false;

    if (*(color++) != '\x1b')
        return false;
    if (*(color++) != '[')
        return false;

    const size_t len = strlen(test);
    if (strncmp(color, test, len) != 0)
        return false;
    color += len;

    if (strcmp(color, "m") != 0)
        return false;

    return true;
}

//------------------------------------------------------------------------------
TEST_CASE("Match colors")
{
    str<> s;

    os::set_env("CLINK_MATCH_COLORS", "fi=1;34:di=93:ro=32:ro ex=1;32:ex=1:hi=1;31:aa*zz=41:*qq=42:z*.md=43");

    parse_match_colors();

    SECTION("basic")
    {
        s.clear();
        REQUIRE(!get_match_color("foo", match_type::none, s));

        s.clear();
        REQUIRE(get_match_color("foo", match_type::file, s));
        REQUIRE(test_color(s, "1;34"));
    }

    SECTION("directory")
    {
        s.clear();
        REQUIRE(get_match_color("foo", match_type::dir, s));
        REQUIRE(test_color(s, "93"));

        s.clear();
        REQUIRE(get_match_color("foo", match_type::dir|match_type::readonly, s));
        REQUIRE(test_color(s, "93"));

        s.clear();
        REQUIRE(get_match_color("foo", match_type::dir|match_type::hidden, s));
        REQUIRE(test_color(s, "1;31"));
    }

    SECTION("executable")
    {
        s.clear();
        REQUIRE(get_match_color("foo.exe", match_type::file, s));
        REQUIRE(test_color(s, "1"));

        s.clear();
        REQUIRE(get_match_color("foo.exe", match_type::file|match_type::readonly, s));
        REQUIRE(test_color(s, "1;32"));

        s.clear();
        REQUIRE(get_match_color("foo.exe", match_type::file|match_type::hidden, s));
        REQUIRE(test_color(s, "1;31"));
    }

    SECTION("pattern")
    {
        s.clear();
        REQUIRE(get_match_color("a a   z z", match_type::file, s));
        REQUIRE(test_color(s, "1;34"));

        s.clear();
        REQUIRE(get_match_color("aa   zz", match_type::file, s));
        REQUIRE(test_color(s, "41"));

        s.clear();
        REQUIRE(get_match_color("qqaaa", match_type::file, s));
        REQUIRE(test_color(s, "1;34"));

        s.clear();
        REQUIRE(get_match_color("aaaqq", match_type::file, s));
        REQUIRE(test_color(s, "42"));

        s.clear();
        REQUIRE(get_match_color("aaaz.md", match_type::file, s));
        REQUIRE(test_color(s, "1;34"));

        s.clear();
        REQUIRE(get_match_color("zaaa.md", match_type::file, s));
        REQUIRE(test_color(s, "43"));
    }
}
