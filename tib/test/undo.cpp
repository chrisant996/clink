// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"

static void invoke_command(tib::editor_context& context, const char* name)
{
    const auto command = tib::editor_context::lookup_command(name);
    REQUIRE(command != nullptr);
    REQUIRE(command(context, 0, name, nullptr) == 0);
}

TEST_CASE("Undo grouping")
{
    tib::editor_context context;
    context.initialize();

    SECTION("Merge insert into previous group")
    {
        const char utf8[] =
            "\xc2\xa2"          // U+00A2, 2 bytes.
            "\xe2\x82\xac"      // U+20AC, 3 bytes.
            "\xf0\x90\x8d\x88"; // U+10348, 4 bytes.
        for (const char c : utf8)
        {
            if (c)
                context.insert_char(c);
        }
        REQUIRE(context.get_text() == tib::cstring(utf8));

        context.undo();
        REQUIRE(context.get_text() == "\xc2\xa2\xe2\x82\xac");

        context.undo();
        REQUIRE(context.get_text() == "\xc2\xa2");

        context.undo();
        REQUIRE(context.get_text().empty());

        context.redo();
        REQUIRE(context.get_text() == "\xc2\xa2");

        context.redo();
        REQUIRE(context.get_text() == "\xc2\xa2\xe2\x82\xac");

        context.redo();
        REQUIRE(context.get_text() == tib::cstring(utf8));
    }

    SECTION("Continuation byte without previous group")
    {
        context.insert_char(char(0x80));
        context.undo();
        REQUIRE(context.get_text().empty());
    }

    SECTION("ASCII bytes remain separate groups")
    {
        context.insert_char('a');
        context.insert_char('b');
        context.undo();
        REQUIRE(context.get_text() == "a");
    }
}

TEST_CASE("Undo all command")
{
    tib::editor_context context;
    context.initialize("original");
    context.set_selection(0, 4);
    context.clear_undo();

    context.insert_text(" one");
    context.insert_text(" two");
    context.set_caret(3);

    invoke_command(context, "undo-all");
    REQUIRE(context.get_text() == "original");
    REQUIRE(context.get_selection_state().get_anchor() == 0);
    REQUIRE(context.get_selection_state().get_caret() == 4);

    SECTION("Redo restores each change individually")
    {
        context.redo();
        REQUIRE(context.get_text() == " oneinal");

        context.redo();
        REQUIRE(context.get_text() == " one twoinal");
        REQUIRE(context.get_selection_state().get_caret() == 8);

        context.undo();
        REQUIRE(context.get_text() == " oneinal");
    }

    SECTION("A new edit replaces the redo records")
    {
        context.insert_text(" replacement");
        context.redo();
        REQUIRE(context.get_text() == " replacementinal");
    }
}

TEST_CASE("Undo all command after undo")
{
    tib::editor_context context;
    context.initialize();
    context.insert_text("one");
    context.insert_text(" two");
    context.insert_text(" three");
    context.undo();
    REQUIRE(context.get_text() == "one two");

    invoke_command(context, "undo-all");
    REQUIRE(context.get_text().empty());

    context.redo();
    REQUIRE(context.get_text() == "one");
    context.redo();
    REQUIRE(context.get_text() == "one two");
    context.redo();
    REQUIRE(context.get_text() == "one two three");
}
