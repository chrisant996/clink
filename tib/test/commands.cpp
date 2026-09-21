// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"

static void initialize_context(tib::editor_context& context, const char* text,
                               tib::textpos_t caret)
{
    context.initialize(text);
    context.set_caret(caret);
}

static void invoke_command(tib::editor_context& context, const char* name)
{
    const auto command = tib::editor_context::lookup_command(name);
    REQUIRE(command != nullptr);
    REQUIRE(command(context, 0, name, nullptr) == 0);
}

TEST_CASE("Abort command resets editor state")
{
    tib::editor_context context;
    tib::border_definition border;
    border.left = "|";
    initialize_context(context, "abc", 2);
    context.set_border(&border);
    context.set_selection(0, 2);
    context.set_overwrite_mode(true);
    context.set_named_value("operation", "pending");
    context.set_numeric_argument(-12);
    context.set_last_command("previous-command");

    invoke_command(context, "abort");

    REQUIRE(context.get_text() == "abc");
    REQUIRE(context.get_selection_state().get_anchor() == 0);
    REQUIRE(context.get_selection_state().get_caret() == 2);
    REQUIRE(context.get_overwrite_mode());
    REQUIRE(context.get_border() == &border);
    REQUIRE(context.get_named_value("operation") == nullptr);
    REQUIRE(!context.has_numeric_argument());
    REQUIRE(!strcmp(context.get_last_command(), ""));
}

TEST_CASE("Abort command interrupts pending overwrite input")
{
    tib::editor_context context;
    initialize_context(context, "ab", 0);
    context.set_overwrite_mode(true);
    context.insert_char(char(0xc2), true);

    invoke_command(context, "abort");
    context.insert_char(char(0xa2), true);
    REQUIRE(context.get_text() == "\xc2\xa2" "b");

    REQUIRE(context.undo());
    REQUIRE(context.get_text() == "\xc2" "b");
}

TEST_CASE("Commands honor numeric arguments")
{
    SECTION("Negative forward-char moves backward the requested count")
    {
        tib::editor_context context;
        initialize_context(context, "abcdef", 5);
        context.set_numeric_argument(-3);
        invoke_command(context, "forward-char");
        REQUIRE(context.get_caret() == 2);
    }

    SECTION("Negative forward-word moves backward the requested count")
    {
        tib::editor_context context;
        initialize_context(context, "one two three four", 18);
        context.set_numeric_argument(-2);
        invoke_command(context, "forward-word");
        REQUIRE(context.get_caret() == 8);
    }

    SECTION("Positive repeated deletion is one undo operation")
    {
        tib::editor_context context;
        initialize_context(context, "abcdef", 1);
        context.set_numeric_argument(3);
        invoke_command(context, "del-char-right");
        REQUIRE(context.get_text() == "aef");
        REQUIRE(context.get_caret() == 1);
        REQUIRE(context.undo());
        REQUIRE(context.get_text() == "abcdef");
        REQUIRE(context.get_caret() == 1);
    }

    SECTION("Negative repeated deletion inverts direction and is one undo operation")
    {
        tib::editor_context context;
        initialize_context(context, "abcdef", 5);
        context.set_numeric_argument(-3);
        invoke_command(context, "del-char-right");
        REQUIRE(context.get_text() == "abf");
        REQUIRE(context.get_caret() == 2);
        REQUIRE(context.undo());
        REQUIRE(context.get_text() == "abcdef");
        REQUIRE(context.get_caret() == 5);
    }
}

TEST_CASE("Select word command")
{
    SECTION("Selects the word containing the caret")
    {
        tib::editor_context context;
        initialize_context(context, "alpha beta", 2);
        const auto command = tib::editor_context::lookup_command("select-word");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "select-word", nullptr) == 0);
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 0);
        REQUIRE(selection.get_sel_end() == 5);
    }

    SECTION("Selects a Unicode word without splitting graphemes")
    {
        static const char text[] = "x \xc3\xa9" "e\xcc\x81" "y z";
        tib::editor_context context;
        initialize_context(context, text, 5);
        const auto command = tib::editor_context::lookup_command("select-word");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "select-word", nullptr) == 0);
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 2);
        REQUIRE(selection.get_sel_end() == 8);
    }

    SECTION("Selects the preceding word at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "alpha beta", 10);
        const auto command = tib::editor_context::lookup_command("select-word");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "select-word", nullptr) == 0);
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 6);
        REQUIRE(selection.get_sel_end() == 10);
    }
}

TEST_CASE("Bigword commands")
{
    SECTION("Forward and backward bigword use whitespace boundaries")
    {
        tib::editor_context context;
        initialize_context(context, "one.two\tthree", 0);
        invoke_command(context, "forward-bigword");
        REQUIRE(context.get_caret() == 7);
        invoke_command(context, "forward-bigword");
        REQUIRE(context.get_caret() == 13);
        invoke_command(context, "backward-bigword");
        REQUIRE(context.get_caret() == 8);
        invoke_command(context, "backward-bigword");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Delete bigword left includes punctuation")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 7);
        invoke_command(context, "del-bigword-left");
        REQUIRE(context.get_text() == " three");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Delete bigword right includes punctuation")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 0);
        invoke_command(context, "del-bigword-right");
        REQUIRE(context.get_text() == " three");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Select bigword includes punctuation")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 4);
        invoke_command(context, "select-bigword");
        const auto& selection = context.get_selection_state();
        REQUIRE(selection.get_sel_begin() == 0);
        REQUIRE(selection.get_sel_end() == 7);
    }

    SECTION("Transpose bigwords treats punctuation as part of each bigword")
    {
        tib::editor_context context;
        initialize_context(context, "one.two three", 8);
        invoke_command(context, "transpose-bigwords");
        REQUIRE(context.get_text() == "three one.two");
        REQUIRE(context.get_caret() == 13);
    }
}

TEST_CASE("Case transform commands")
{
    SECTION("Upper case transforms the selection")
    {
        tib::editor_context context;
        initialize_context(context, "one mIxEd three", 9);
        context.set_selection(9, 4);
        invoke_command(context, "upper-case");
        REQUIRE(context.get_text() == "one MIXED three");
        REQUIRE(context.get_selection_state().get_anchor() == 9);
        REQUIRE(context.get_selection_state().get_caret() == 4);
        REQUIRE(context.get_selection_state().has_selection());
    }

    SECTION("Lower case transforms the selection")
    {
        tib::editor_context context;
        initialize_context(context, "one MIXED three", 4);
        context.set_selection(4, 9);
        invoke_command(context, "lower-case");
        REQUIRE(context.get_text() == "one mixed three");
        REQUIRE(context.get_selection_state().get_anchor() == 4);
        REQUIRE(context.get_selection_state().get_caret() == 9);
        REQUIRE(context.get_selection_state().has_selection());
    }

    SECTION("Capitalize transforms the selection to title case")
    {
        tib::editor_context context;
        initialize_context(context, "one mIxEd caSE three", 4);
        context.set_selection(4, 14);
        invoke_command(context, "capitalize");
        REQUIRE(context.get_text() == "one Mixed Case three");
        REQUIRE(context.get_selection_state().get_anchor() == 4);
        REQUIRE(context.get_selection_state().get_caret() == 14);
        REQUIRE(context.get_selection_state().has_selection());
    }

    SECTION("No selection transforms from the care to the end of the word")
    {
        tib::editor_context context;
        initialize_context(context, "one mIxxEd three", 7);
        invoke_command(context, "upper-case");
        REQUIRE(context.get_text() == "one mIxXED three");
        REQUIRE(context.get_selection_state().get_anchor() == 10);
        REQUIRE(context.get_selection_state().get_caret() == 10);
    }

    SECTION("No selection transforms the next word from whitespace")
    {
        tib::editor_context context;
        initialize_context(context, "one   MIXED three", 4);
        invoke_command(context, "lower-case");
        REQUIRE(context.get_text() == "one   mixed three");
        REQUIRE(context.get_selection_state().get_anchor() == 11);
        REQUIRE(context.get_selection_state().get_caret() == 11);
    }

    SECTION("No selection is no-op at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "one mIXED", 9);
        invoke_command(context, "capitalize");
        REQUIRE(context.get_text() == "one mIXED");
        REQUIRE(context.get_selection_state().get_anchor() == 9);
        REQUIRE(context.get_selection_state().get_caret() == 9);
    }

    SECTION("Toggle case cycles through upper title and lower case")
    {
        tib::editor_context context;
        initialize_context(context, "mIxEd caSE", 0);

        context.set_selection(0, 10);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "MIXED CASE");
        REQUIRE(context.get_selection_state().get_anchor() == 0);
        REQUIRE(context.get_selection_state().get_caret() == 10);

        context.set_selection(10, 0);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "Mixed Case");
        REQUIRE(context.get_selection_state().get_anchor() == 10);
        REQUIRE(context.get_selection_state().get_caret() == 0);

        context.set_selection(0, 10);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "mixed case");
        REQUIRE(context.get_selection_state().get_anchor() == 0);
        REQUIRE(context.get_selection_state().get_caret() == 10);

        context.set_selection(10, 0);
        invoke_command(context, "toggle-case");
        REQUIRE(context.get_text() == "MIXED CASE");
        REQUIRE(context.get_selection_state().get_anchor() == 10);
        REQUIRE(context.get_selection_state().get_caret() == 0);
    }

    SECTION("A case transform is one undo operation")
    {
        tib::editor_context context;
        initialize_context(context, "one mIXED three", 6);
        invoke_command(context, "capitalize");
        REQUIRE(context.get_text() == "one mIXed three");
        context.undo();
        REQUIRE(context.get_text() == "one mIXED three");
        REQUIRE(context.get_selection_state().get_anchor() == 6);
        REQUIRE(context.get_selection_state().get_caret() == 6);
    }
}

TEST_CASE("Screen line cursor column continuation")
{
    static const char input_bytes[] = "\x1b[B\x1b[1;2B"; // Down, Shift-Down.
    test_input_stream stream(input_bytes);

    auto context = std::make_shared<tib::editor_context>();
    context->set_max_width(10);
    context->set_max_height(3);
    context->set_variable_height(true);
    initialize_context(*context, "abcdefgh\nxy\nabcdefgh", 7);
    context->set_bindings(tib::make_default_key_table());

    tib::binding_resolver resolver;
    resolver.add_target(context);

    while (tib::term_in_avail())
    {
        context->display();
        const int32_t c = tib::term_in();
        REQUIRE(c >= 0);
        auto resolved = resolver.step(uint8_t(c));
        if (!resolved.more())
            REQUIRE(resolved.dispatch());
    }

    REQUIRE(stream.empty());
    REQUIRE(!strcmp(context->get_last_command(), "cua-screen-line-down"));
    REQUIRE(context->get_selection_state().get_anchor() == 11);
    REQUIRE(context->get_selection_state().get_caret() == 19);
}

TEST_CASE("Transpose chars command")
{
    SECTION("Swaps the grapheme at the caret with the preceding grapheme")
    {
        tib::editor_context context;
        initialize_context(context, "abcd", 2);
        const auto command = tib::editor_context::lookup_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "acbd");
        REQUIRE(context.get_caret() == 3);
    }

    SECTION("Swaps two graphemes before the caret at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "abcd", 4);
        const auto command = tib::editor_context::lookup_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "abdc");
        REQUIRE(context.get_caret() == 4);
    }

    SECTION("Treats a combining sequence as one grapheme")
    {
        static const char text[] = "a" "e\xcc\x81" "b";
        tib::editor_context context;
        initialize_context(context, text, 4);
        const auto command = tib::editor_context::lookup_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "ab" "e\xcc\x81");
        REQUIRE(context.get_caret() == 5);
    }

    SECTION("Does nothing without two graphemes")
    {
        tib::editor_context context;
        initialize_context(context, "a", 1);
        const auto command = tib::editor_context::lookup_command("transpose-chars");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-chars", nullptr) == 0);
        REQUIRE(context.get_text() == "a");
        REQUIRE(context.get_caret() == 1);
    }
}

TEST_CASE("Transpose words command")
{
    SECTION("Swaps the previous word with a word beginning at the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 4);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one three");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Swaps the words on either side of the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one,  two three", 4);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two,  one three");
        REQUIRE(context.get_caret() == 9);
    }

    SECTION("Swaps the surrounding words at the end of a word")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 3);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one three");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Swaps the preceding word with the word containing the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one,  two three", 7);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two,  one three");
        REQUIRE(context.get_caret() == 9);
    }

    SECTION("Does nothing at the beginning of the first word")
    {
        tib::editor_context context;
        initialize_context(context, "one,  two three", 0);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one,  two three");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Swaps the two preceding words at end of input")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 13);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one three two");
        REQUIRE(context.get_caret() == 13);
    }

    SECTION("Uses the last word and its predecessor when no word follows")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 10);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one three two");
        REQUIRE(context.get_caret() == 13);
    }

    SECTION("Swaps the previous word with the last word beginning at the caret")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 8);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one three two");
        REQUIRE(context.get_caret() == 13);
    }

    SECTION("Does nothing without a word before leading separators")
    {
        tib::editor_context context;
        initialize_context(context, "  one two", 0);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "  one two");
        REQUIRE(context.get_caret() == 0);
    }

    SECTION("Swap the preceding words after trailing separators")
    {
        tib::editor_context context;
        initialize_context(context, "one$two,,", 9);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two$one,,");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Swaps the preceding words from within trailing separators")
    {
        tib::editor_context context;
        initialize_context(context, "one two  ", 8);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one  ");
        REQUIRE(context.get_caret() == 7);
    }

    SECTION("Treats Unicode graphemes as word characters")
    {
        static const char text[] = "a" "e\xcc\x81" " \xc3\xa9" "b";
        tib::editor_context context;
        initialize_context(context, text, 6);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "\xc3\xa9" "b a" "e\xcc\x81");
        REQUIRE(context.get_caret() == 8);
    }

    SECTION("Does nothing without two words")
    {
        tib::editor_context context;
        initialize_context(context, "one", 3);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "one");
        REQUIRE(context.get_caret() == 3);
    }

    SECTION("Does nothing without any words")
    {
        tib::editor_context context;
        initialize_context(context, "  , ", 2);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "  , ");
        REQUIRE(context.get_caret() == 2);
    }

    SECTION("Uses the caret end of a selection and clears the selection")
    {
        tib::editor_context context;
        initialize_context(context, "one two three", 5);
        context.set_selection(0, 5);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        REQUIRE(context.get_text() == "two one three");
        REQUIRE(context.get_caret() == 7);
        REQUIRE(!context.get_selection_state().has_selection());
    }

    SECTION("Undo restores a transpose as one operation")
    {
        tib::editor_context context;
        initialize_context(context, "one two", 4);
        const auto command = tib::editor_context::lookup_command("transpose-words");
        REQUIRE(command != nullptr);
        REQUIRE(command(context, 0, "transpose-words", nullptr) == 0);
        context.undo();
        REQUIRE(context.get_text() == "one two");
        REQUIRE(context.get_caret() == 4);
    }
}
