// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "tib.h"

static void invoke_command(tib::editor_context& context, const char* name)
{
    const auto command = tib::editor_context::lookup_command(name);
    REQUIRE(command != nullptr);
    REQUIRE(command(context, 0, name, nullptr) == 0);
}

TEST_CASE("Overwrite mode state")
{
    tib::editor_context context;
    REQUIRE(!context.get_overwrite_mode());

    context.set_overwrite_mode(true);
    REQUIRE(context.get_overwrite_mode());

    invoke_command(context, "toggle-overwrite-mode");
    REQUIRE(!context.get_overwrite_mode());

    context.set_overwrite_mode(true);
    context.initialize();
    REQUIRE(!context.get_overwrite_mode());
}

TEST_CASE("Selection navigation counter")
{
    tib::selection_state selection(1);
    const tib::selection_state original(selection);

    const uint32_t initial = selection.get_navigation_counter();
    REQUIRE(selection.set_caret(2));
    REQUIRE(selection.get_navigation_counter() != initial);

    const uint32_t before_restore = selection.get_navigation_counter();
    selection = original;
    REQUIRE(selection.get_caret() == 1);
    REQUIRE(selection.get_navigation_counter() != before_restore);
}

TEST_CASE("Overwrite text")
{
    tib::editor_context context;
    context.initialize("abcd");
    context.set_caret(1);

    SECTION("Insert remains the API default")
    {
        context.insert_text("XY");
        REQUIRE(context.get_text() == "aXYbcd");
        REQUIRE(context.get_caret() == 3);
    }

    SECTION("Replaces one destination grapheme per source grapheme")
    {
        context.insert_text("XY", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "aXYd");
        REQUIRE(context.get_caret() == 3);
    }

    SECTION("Inserts after exhausting the destination")
    {
        context.set_caret(3);
        context.insert_text("XYZ", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "abcXYZ");
    }

    SECTION("Selection replacement does not consume following text")
    {
        context.set_selection(1, 3);
        context.insert_text("X", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "aXd");
    }

    SECTION("Replacement is available at maximum length")
    {
        context.set_max_length(4);
        context.insert_text("XYZ", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "aXYZ");
    }

    SECTION("Undo and redo treat replacement atomically")
    {
        context.insert_text("XY", tib::c_auto_length, true);
        context.undo();
        REQUIRE(context.get_text() == "abcd");
        REQUIRE(context.get_caret() == 1);
        context.redo();
        REQUIRE(context.get_text() == "aXYd");
        REQUIRE(context.get_caret() == 3);
    }
}

TEST_CASE("Overwrite respects grapheme boundaries")
{
    tib::editor_context context;

    SECTION("A combining destination is replaced as one grapheme")
    {
        context.initialize("a" "e\xcc\x81" "z");
        context.set_caret(1);
        context.insert_text("X", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "aXz");
    }

    SECTION("A combining source replaces one grapheme")
    {
        context.initialize("abz");
        context.set_caret(1);
        context.insert_text("e\xcc\x81", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "a" "e\xcc\x81" "z");
    }

    SECTION("A combining mark extends preceding input without another replacement")
    {
        context.initialize("ab");
        context.set_caret(0);
        context.insert_char('e', true);
        context.insert_char(char(0xcc), true);
        context.insert_char(char(0x81), true);
        REQUIRE(context.get_text() == "e\xcc\x81" "b");
        context.undo();
        REQUIRE(context.get_text() == "eb");
    }

    SECTION("A multibyte insert_char replaces only one grapheme")
    {
        context.initialize("ab");
        context.set_caret(0);
        context.insert_char(char(0xc2), true);
        REQUIRE(context.get_text() == "\xc2" "b");
        context.insert_char(char(0xa2), true);
        REQUIRE(context.get_text() == "\xc2\xa2" "b");
        context.undo();
        REQUIRE(context.get_text() == "ab");
        context.redo();
        REQUIRE(context.get_text() == "\xc2\xa2" "b");
    }

    SECTION("Navigation interrupts bytewise overwrite accumulation")
    {
        context.initialize("ab");
        context.set_caret(0);
        context.insert_char(char(0xc2), true);
        REQUIRE(context.move_right());
        REQUIRE(context.move_left());
        context.insert_char(char(0xa2), true);
        REQUIRE(context.get_text() == "\xc2\xa2" "b");
        context.undo();
        REQUIRE(context.get_text() == "\xc2" "b");
    }

    SECTION("Undo interrupts bytewise overwrite accumulation")
    {
        context.initialize("ab");
        context.set_caret(0);
        context.insert_char(char(0xc2), true);
        context.undo();
        REQUIRE(context.get_text() == "ab");
        context.insert_char(char(0xa2), true);
        REQUIRE(context.get_text() == "\xa2" "b");
    }

    SECTION("Invalid UTF8 follows str_iter replacement boundaries")
    {
        context.initialize("abcd");
        context.set_caret(0);
        context.insert_char(char(0xe0), true);
        context.insert_char(char(0x80), true);
        context.insert_char(char(0x80), true);
        REQUIRE(context.get_text() == "\xe0\x80\x80" "d");
    }

    SECTION("Truncated UTF8 remains one replacement until a new character")
    {
        context.initialize("abcd");
        context.set_caret(0);
        context.insert_char(char(0xe1), true);
        context.insert_char(char(0x80), true);
        REQUIRE(context.get_text() == "\xe1\x80" "bcd");
        context.insert_char('X', true);
        REQUIRE(context.get_text() == "\xe1\x80" "Xcd");
    }

    SECTION("A ZWJ emoji sequence replaces one grapheme")
    {
        const char emoji[] = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
        context.initialize("ab");
        context.set_caret(0);
        context.insert_text(emoji, tib::c_auto_length, true);
        tib::cstring expected(emoji);
        expected.append("b");
        REQUIRE(context.get_text() == expected);
    }
}

TEST_CASE("Overwrite preserves line boundaries")
{
    tib::editor_context context;
    context.initialize("ab\ncd");

    SECTION("Does not consume an existing newline")
    {
        context.set_caret(1);
        context.insert_text("XYZ", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "aXYZ\ncd");
    }

    SECTION("An input newline does not consume destination text")
    {
        context.set_caret(0);
        context.insert_text("x\ny", tib::c_auto_length, true);
        REQUIRE(context.get_text() == "x\ny\ncd");
    }
}
