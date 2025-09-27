// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <core/base.h>
#include <terminal/ecma48_iter.h>

#include <new>

static ecma48_state g_state;

//------------------------------------------------------------------------------
TEST_CASE("ecma48 chars")
{
    const char* input = "abc123";

    const ecma48_code* code;

    ecma48_iter iter(input, g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_pointer() == input);
    REQUIRE(code->get_length() == 6);

    REQUIRE(!iter.next());
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c0")
{
    const ecma48_code* code;

    ecma48_iter iter("\x01 \x10\x1f", g_state);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 0x01);
    REQUIRE(code->get_length() == 1);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 1);
    REQUIRE(code->get_pointer()[0] == ' ');

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 0x10);
    REQUIRE(code->get_length() == 1);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 0x1f);
    REQUIRE(code->get_length() == 1);

    REQUIRE(!iter.next());
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 (simple)")
{
    const ecma48_code* code;

    ecma48_iter iter("\x1b\x40\x1b\x51\x1b\x5c", g_state);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x40);
    REQUIRE(code->get_length() == 2);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x51);
    REQUIRE(code->get_length() == 2);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x5c);
    REQUIRE(code->get_length() == 2);

    REQUIRE(!iter.next());
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 icf")
{
    const ecma48_code* code;

    ecma48_iter iter("\x1b\x60\x1b\x7f", g_state);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_icf);
    REQUIRE(code->get_code() == 0x60);
    REQUIRE(code->get_length() == 2);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_icf);
    REQUIRE(code->get_code() == 0x7f);
    REQUIRE(code->get_length() == 2);

    REQUIRE(!iter.next());
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 csi")
{
    const ecma48_code* code;
    ecma48_code::csi<8> csi;

    ecma48_iter iter("\x1b[\x40\xc2\x9b\x20\x7e", g_state);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_length() == 3);

    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.final == 0x40);
    REQUIRE(csi.param_count == 0);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_length() == 4);

    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == 0);
    REQUIRE(csi.intermediate == 0x20);
    REQUIRE(csi.final == 0x7e);

    REQUIRE(!iter.next());
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 csi params")
{
    const ecma48_code* code;
    ecma48_code::csi<8> csi;

    // ---
    ecma48_iter iter("\x1b[123\x7e", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_length() == 6);

    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == 1);
    REQUIRE(csi.params[0] == 123);

    // ---
    new (&iter) ecma48_iter("\x1b[1;12;123 \x40", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_length() == 12);

    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == 3);
    REQUIRE(csi.params[0] == 1);
    REQUIRE(csi.params[1] == 12);
    REQUIRE(csi.params[2] == 123);
    REQUIRE(csi.intermediate == 0x20);
    REQUIRE(csi.final == 0x40);

    // ---
    new (&iter) ecma48_iter("\x1b[;@", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_length() == 4);

    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == 2);
    REQUIRE(csi.params[0] == 0);
    REQUIRE(csi.params[1] == 0);
    REQUIRE(csi.final == '@');

    // Overflow
    new (&iter) ecma48_iter("\x1b[;;;;;;;;;;;;1;2;3;4;5 m", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_length() == 25);

    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == decltype(csi)::max_param_count);

    new (&iter) ecma48_iter("\x1b[1;2;3;4;5;6;7;8;1;2;3;4;5;6;7;8m", g_state);
    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == decltype(csi)::max_param_count);
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 csi invalid")
{
    const ecma48_code* code;

    ecma48_iter iter("\x1b[1;2\01", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c0);
    REQUIRE(code->get_code() == 1);
    REQUIRE(code->get_pointer()[0] == 1);
    REQUIRE(code->get_length() == 1);
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 csi stream")
{
    const char input[] = "\x1b[1;21m";

    ecma48_iter iter_1(input, g_state, 0);
    for (int32 i = 0; i < sizeof_array(input) - 1; ++i)
    {
        const ecma48_code* code;

        memset(&iter_1, 0xab, sizeof(iter_1));
        new (&iter_1) ecma48_iter(input, g_state, i);
        REQUIRE(!iter_1.next());

        ecma48_iter iter_2(input + i, g_state, sizeof_array(input) - i);
        code = &iter_2.next();
        REQUIRE(*code);
        REQUIRE(code->get_type() == ecma48_code::type_c1);
        REQUIRE(code->get_length() == 7);

        ecma48_code::csi<8> csi;
        REQUIRE(code->decode_csi(csi));
        REQUIRE(csi.param_count == 2);
        REQUIRE(csi.params[0] == 1);
        REQUIRE(csi.params[1] == 21);
        REQUIRE(csi.final == 'm');
    }
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 csi split")
{
    const ecma48_code* code;

    ecma48_iter iter(" \x1b[1;2x@@@@", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 1);
    REQUIRE(code->get_pointer()[0] == ' ');

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);

    ecma48_code::csi<8> csi;
    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == 2);
    REQUIRE(csi.params[0] == 1);
    REQUIRE(csi.params[1] == 2);
    REQUIRE(csi.final == 'x');

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 4);
    REQUIRE(code->get_pointer()[0] == '@');

    REQUIRE(!iter.next());
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 csi private use")
{
    struct {
        const char* input;
        int32 param_count;
        int32 params[8];
    } tests[] = {
        { "\x1b[?x",               0 },
        { "\x1b[?99x",             1, 99 },
        { "\x1b[?;98x",            2, 0, 98 },
        { "\x1b[?1;2;3x",          3, 1, 2, 3 },
        { "\x1b[?2;?4;6x",         3, 2, 4, 6 },
        { "\x1b[?3;6?;9x",         3, 3, 6, 9 },
        { "\x1b[?4;8?;???3;13??x", 4, 4, 8, 3, 13 },
    };

    for (auto test : tests)
    {
        const ecma48_code* code;
        ecma48_iter iter(test.input, g_state);
        code = &iter.next();
        REQUIRE(*code);

        ecma48_code::csi<8> csi;
        REQUIRE(code->decode_csi(csi));
        REQUIRE(csi.private_use);

        REQUIRE(csi.param_count == test.param_count);
        for (int32 i = 0; i < csi.param_count; ++i)
            REQUIRE(csi.params[i] == test.params[i]);
    }
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 c1 !csi")
{
    const ecma48_code* code;

    const char* terminators[] = { "\x1b\\", "\xc2\x9c" };
    for (int32 i = 0, n = sizeof_array(terminators); i < n; ++i)
    {
        const char* announcers[] = {
            "\x1b\x5f", "\xc2\x9f",
            "\x1b\x50", "\xc2\x90",
            "\x1b\x5d", "\xc2\x9d",
            "\x1b\x5e", "\xc2\x9e",
            "\x1b\x58", "\xc2\x98",
        };
        for (int32 j = 0, m = sizeof_array(announcers); j < m; ++j)
        {
            str<> input;
            input << announcers[j];
            input << "xyz";
            input << terminators[i];

            ecma48_iter iter(input.c_str(), g_state);
            code = &iter.next();
            REQUIRE(*code);
            REQUIRE(code->get_length() == input.length());
            str<> ctrl_str;
            code->get_c1_str(ctrl_str);
            REQUIRE(ctrl_str.equals("xyz"));

            REQUIRE(!iter.next());
        }
    }
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 utf8")
{
    const ecma48_code* code;

    ecma48_iter iter("\xc2\x9c", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == 0x5c);
    REQUIRE(code->get_length() == 2);

    new (&iter) ecma48_iter("\xc2\x9bz", g_state);
    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_length() == 3);

    ecma48_code::csi<8> csi;
    REQUIRE(code->decode_csi(csi));
    REQUIRE(csi.param_count == 0);
    REQUIRE(csi.final == 'z');
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 conemu")
{
    static const char nested[] = "abc \x1b]0;\x1b]9;8;\"USERNAME\"\x1b\\@\x1b]9;8;\"COMPUTERNAME\"\x1b\\ /CMD 10.0/\x1b\\ xyz";

    const ecma48_code* code;

    ecma48_iter iter(nested, g_state);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 4);

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_c1);
    REQUIRE(code->get_code() == ecma48_code::c1_osc);
    REQUIRE(code->get_length() == 58);

    ecma48_code::osc osc;
    REQUIRE(code->decode_osc(osc));
    REQUIRE(osc.command == '0');
    REQUIRE(osc.subcommand == 0);
    REQUIRE(osc.visible == false);

    str<> result;
    const char* uname = getenv("USERNAME");
    const char* cname = getenv("COMPUTERNAME");
    result.format("%s@%s /CMD 10.0/", uname ? uname : "", cname ? cname : "");
    REQUIRE(result.equals(osc.param.c_str()));

    code = &iter.next();
    REQUIRE(*code);
    REQUIRE(code->get_type() == ecma48_code::type_chars);
    REQUIRE(code->get_length() == 4);
}

//------------------------------------------------------------------------------
TEST_CASE("ecma48 osc envvar")
{
    static const char* const c_strings[] =
    {
        "ab\x1b]9;8;\"COMPUTERNAME\"\x07xy",
        "ab\x1b]9;8;COMPUTERNAME\x07xy",
        "ab\x1b]9;8;\"COMPUTERNAME\"\x1b\\xy",
        "ab\x1b]9;8;COMPUTERNAME\x1b\\xy",
    };

    for (const char* s : c_strings)
    {
        const ecma48_code* code;

        ecma48_iter iter(s, g_state);

        code = &iter.next();
        REQUIRE(*code);
        REQUIRE(code->get_type() == ecma48_code::type_chars);
        REQUIRE(code->get_length() == 2);

        code = &iter.next();
        REQUIRE(*code);
        REQUIRE(code->get_type() == ecma48_code::type_c1);
        REQUIRE(code->get_code() == ecma48_code::c1_osc);

        ecma48_code::osc osc;
        REQUIRE(code->decode_osc(osc));
        REQUIRE(osc.command == '9');
        REQUIRE(osc.subcommand == '8');
        REQUIRE(osc.visible == true);

        const char* compname = getenv("COMPUTERNAME");
        REQUIRE(compname);
        REQUIRE(osc.output.equals(compname));

        code = &iter.next();
        REQUIRE(*code);
        REQUIRE(code->get_type() == ecma48_code::type_chars);
        REQUIRE(code->get_length() == 2);
    }
}
