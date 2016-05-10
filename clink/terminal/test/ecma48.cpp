// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <terminal/ecma48_iter.h>

static ecma48_state g_state;

TEST_CASE("ecma48 chars") {
    const char* input = "abc123";

    const ecma48_code* code;

    ecma48_iter iter(input, g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_str() == input);
    REQUIRE(code->get_length() == 6);

    REQUIRE(iter.next() == nullptr);
}

TEST_CASE("ecma48 c0") {
    const ecma48_code* code;

    ecma48_iter iter("\x01 \x10\x1f", g_state);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 0x01);
    REQUIRE(code->get_length() == 1);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 1);
    REQUIRE(code->get_str()[0] == ' ');

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 0x10);
    REQUIRE(code->get_length() == 1);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 0x1f);
    REQUIRE(code->get_length() == 1);

    REQUIRE(iter.next() == nullptr);
}

TEST_CASE("ecma48 c1 (simple)") {
    const ecma48_code* code;

    ecma48_iter iter("\x1b\x40\x1b\x51\x1b\x5c", g_state);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x40);
    REQUIRE(code->get_length() == 2);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x51);
    REQUIRE(code->get_length() == 2);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x5c);
    REQUIRE(code->get_length() == 2);

    REQUIRE(iter.next() == nullptr);
}

TEST_CASE("ecma48 icf") {
    const ecma48_code* code;

    ecma48_iter iter("\x1b\x60\x1b\x7f", g_state);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_icf);
    REQUIRE(code->get_code() == 0x60);
    REQUIRE(code->get_length() == 2);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_icf);
    REQUIRE(code->get_code() == 0x7f);
    REQUIRE(code->get_length() == 2);

    REQUIRE(iter.next() == nullptr);
}

TEST_CASE("ecma48 csi") {
    const ecma48_code* code;
    int final, params[8], param_count;

    ecma48_iter iter("\x1b[\x40\xc2\x9b\x20\x7e", g_state);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_length() == 1);

    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(final == 0x40);
    REQUIRE(param_count == 0);

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_length() == 2);

    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(final == 0x207e);
    REQUIRE(param_count == 0);

    REQUIRE(iter.next() == nullptr);
}

TEST_CASE("ecma48 csi params") {
    const ecma48_code* code;
    int final, params[8], param_count;

    // ---
    ecma48_iter iter("\x1b[123\x7e", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_length() == 4);

    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(param_count == 1);
    REQUIRE(params[0] == 123);

    // ---
    new (&iter) ecma48_iter("\x1b[1;12;123 \x40", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_length() == 10);

    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(param_count == 3);
    REQUIRE(params[0] == 1);
    REQUIRE(params[1] == 12);
    REQUIRE(params[2] == 123);
    REQUIRE(final == 0x2040);

    // ---
    new (&iter) ecma48_iter("\x1b[;@", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_length() == 2);

    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(param_count == 2);
    REQUIRE(params[0] == 0);
    REQUIRE(params[1] == 0);
    REQUIRE(final == '@');

    // ---
    new (&iter) ecma48_iter("\x1b[;;;;;;;;;;;;1;2;3;4;5 m", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_length() == 23);

    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(param_count == sizeof_array(params));
}

TEST_CASE("ecma48 csi invalid") {
    const ecma48_code* code;

    ecma48_iter iter("\x1b[1;2\01", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 1);
    REQUIRE(code->get_str()[0] == 1);
    REQUIRE(code->get_length() == 1);
}

#if 0 // MODE4
TEST_CASE("ecma48 stream") {
    const ecma48_code* code;

    ecma48_iter iter("\x1b[1;2", g_state);
    REQUIRE(iter.next() == nullptr);

    new (&iter) ecma48_iter("1m", g_state);
    REQUIRE((code = iter.next()) != nullptr);

    REQUIRE(code->get_type() == ecma48_code::type_csi);
    REQUIRE(code->csi->func == 'm');
    REQUIRE(code->csi->param_count == 2);
    REQUIRE(code->csi->params[0] == 1);
    REQUIRE(code->csi->params[1] == 21);
    REQUIRE(code->get_length() == 2);
}

TEST_CASE("ecma48 split") {
    const ecma48_code* code;

    ecma48_iter iter(" \x1b[1;2x@@@@", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 1);
    REQUIRE(code->get_str()[0] == ' ');

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_csi);
    REQUIRE(code->csi->func == 'x');

    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 4);
    REQUIRE(code->get_str()[0] == '@');

    REQUIRE(iter.next() == nullptr);
}
#endif // MODE4

TEST_CASE("ecma48 utf8") {
    const ecma48_code* code;

    ecma48_iter iter("\xc2\x9c", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x5c);
    REQUIRE(code->get_length() == 2);

    new (&iter) ecma48_iter("\xc2\x9bz", g_state);
    REQUIRE((code = iter.next()) != nullptr);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_length() == 1);

    int final, params[8], param_count;
    param_count = code->decode_csi(final, params, sizeof_array(params));
    REQUIRE(param_count == 0);
    REQUIRE(final == 'z');
}
