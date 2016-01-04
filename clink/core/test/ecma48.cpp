// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/ecma48_iter.h>
#include <core/base.h>

TEST_CASE("Ecma48 chars") {
    ecma48_state state;
    ecma48_code code;

    SECTION("chars") {
        const char* input = "abc123";

        ecma48_iter iter(input, state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_chars);
        REQUIRE(code.str == input);
        REQUIRE(code.length == 6);

        REQUIRE(iter.next(code) == false);
    }

    SECTION("c0") {
        ecma48_iter iter("\x01 \x10\x1f", state);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c0);
        REQUIRE(code.c0 == 0x01);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_chars);
        REQUIRE(code.length == 1);
        REQUIRE(code.str[0] == ' ');

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c0);
        REQUIRE(code.c0 == 0x10);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c0);
        REQUIRE(code.c0 == 0x1f);

        REQUIRE(iter.next(code) == false);
    }

    SECTION("c1") {
        ecma48_iter iter("\x1b\x40\x1b\x50\x1b\x5f", state);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c1);
        REQUIRE(code.c1 == 0x40);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c1);
        REQUIRE(code.c1 == 0x50);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c1);
        REQUIRE(code.c1 == 0x5f);

        REQUIRE(iter.next(code) == false);
    }

    SECTION("icf") {
        ecma48_iter iter("\x1b\x60\x1b\x7f", state);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_icf);
        REQUIRE(code.icf == 0x60);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_icf);
        REQUIRE(code.icf == 0x7f);

        REQUIRE(iter.next(code) == false);
    }

    SECTION("csi") {
        ecma48_iter iter("\x1b[\x40\x1b[\x20\x7e", state);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_csi);
        REQUIRE(code.csi->func == 0x40);
        REQUIRE(code.csi->param_count == 0);

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_csi);
        REQUIRE(code.csi->func == 0x207e);
        REQUIRE(code.csi->param_count == 0);

        REQUIRE(iter.next(code) == false);
    }

    SECTION("csi params") {
        ecma48_iter iter("\x1b[1;12;123\x40", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.csi->param_count == 3);
        REQUIRE(code.csi->params[0] == 1);
        REQUIRE(code.csi->params[1] == 12);
        REQUIRE(code.csi->params[2] == 123);

        new (&iter) ecma48_iter("\x1b[;@", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.csi->param_count == 2);
        REQUIRE(code.csi->params[0] == 0);
        REQUIRE(code.csi->params[1] == 0);

        new (&iter) ecma48_iter("\x1b[;;;;;;;;;;;;1;2;3;4;5m", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.csi->param_count == 8);
    }

    SECTION("csi invalid") {
        ecma48_iter iter("\x1b[1;2\01", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c0);
        REQUIRE(code.c0 == 1);
    }

    SECTION("stream") {
        ecma48_iter iter("\x1b[1;2", state);
        REQUIRE(iter.next(code) == false);

        new (&iter) ecma48_iter("1m", state);
        REQUIRE(iter.next(code) == true);

        REQUIRE(code.type == ecma48_code::type_csi);
        REQUIRE(code.csi->func == 'm');
        REQUIRE(code.csi->param_count == 2);
        REQUIRE(code.csi->params[0] == 1);
        REQUIRE(code.csi->params[1] == 21);
    }

    SECTION("split") {
        ecma48_iter iter(" \x1b[1;2x@@@@", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_chars);
        REQUIRE(code.length == 1);
        REQUIRE(code.str[0] == ' ');

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_csi);
        REQUIRE(code.csi->func == 'x');

        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_chars);
        REQUIRE(code.length == 4);
        REQUIRE(code.str[0] == '@');

        REQUIRE(iter.next(code) == false);
    }

    SECTION("utf8") {
        ecma48_iter iter("\xc2\x9bz", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_csi);
        REQUIRE(code.csi->func == 'z');

        new (&iter) ecma48_iter("\xc2\x9c", state);
        REQUIRE(iter.next(code) == true);
        REQUIRE(code.type == ecma48_code::type_c1);
        REQUIRE(code.c1 == 0x5c);
    }
}
