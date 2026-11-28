// Copyright (c) 2020 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "line_editor_tester.h"
#include "editor_module.h"
#include "line_state.h"
#include "word_collector.h"
#include "rl_integration.h"
#include "history_list.h"

#include <core/array.h>

//------------------------------------------------------------------------------
#define CTRL_A "\x01"
#define CTRL_N "\x0e"
#define CTRL_O "\x0f"
#define CTRL_P "\x10"
#define CTRL_Q "\x11"
#define CTRL_U "\x15"
#define CTRL_X "\x18"
#define ESC    "\x1b"

//------------------------------------------------------------------------------
struct delim_module
    : public editor_module
{
    virtual void    bind_input(binder& binder) override {}
    virtual void    on_begin_line(const context& context) override {}
    virtual void    on_end_line() override {}
    virtual void    on_need_input(int32& bind_group) override {}
    virtual void    on_input(const input& input, result& result, const context& context) override {}
    virtual void    on_matches_changed(const context& context, const line_state& line, const char* needle) override;
    virtual void    on_terminal_resize(int32 columns, int32 rows, const context& context) override {}
    virtual void    on_signal(int32 sig) override {}
    uint8           delim = 'a';
};

//------------------------------------------------------------------------------
void delim_module::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
    //const line_state& line = context.line;

    uint32 word_count = line.get_word_count();
    REQUIRE(word_count > 0);

    const word* word = &line.get_words().back();
    delim = word->delim;
}



//------------------------------------------------------------------------------
TEST_CASE("editor")
{
    line_editor::desc desc(nullptr);
    //desc.word_delims = " =";
    line_editor_tester tester(desc, nullptr, " =");

    delim_module module;
    line_editor* editor = tester.get_editor();
    editor->add_module(module);

    SECTION("first")
    {
        SECTION("0") { tester.set_input(""); }
        SECTION("1") { tester.set_input("one"); }
        SECTION("2") { tester.set_input("one t\b\b"); }

        tester.run(true);
        REQUIRE(module.delim == '\0');
    }

    SECTION("space")
    {
        SECTION("0") { tester.set_input("one "); }
        SECTION("1") { tester.set_input("one t\b"); }
        SECTION("2") { tester.set_input("one two"); }
        SECTION("3") { tester.set_input("one two t\b\b"); }
        SECTION("4") { tester.set_input("one= "); }
        SECTION("5") { tester.set_input("one= two"); }
        SECTION("6") { tester.set_input("one= two t\b\b"); }
        SECTION("7") { tester.set_input("one= = two"); }

        tester.run(true);
        REQUIRE(module.delim == ' ');
    }

    SECTION("equals")
    {
        SECTION("0") { tester.set_input("one two="); }
        SECTION("1") { tester.set_input("one two ="); }
        SECTION("2") { tester.set_input("one two=t\b"); }
        SECTION("3") { tester.set_input("one two =t\b"); }
        SECTION("4") { tester.set_input("one two=three"); }
        SECTION("5") { tester.set_input("one two=three f\b\b"); }

        tester.run(true);
        REQUIRE(module.delim == '=');
    }
}

//------------------------------------------------------------------------------
TEST_CASE("editor ambiguous binding")
{
    line_editor_tester tester;
    line_editor* editor = tester.get_editor();

    REQUIRE(editor->update());

    MAKE_CLEANUP([](){
        clink_bind_macro("a", nullptr, emacs_table);
        clink_bind_macro("aa", nullptr, emacs_table);
    });
    clink_bind_macro("a", "ABC", emacs_table);
    clink_bind_macro("aa", "XYZ", emacs_table);

    SECTION("no timeout")
    {
        tester.set_input("aa");
        tester.set_expected_output("XYZ");
        tester.run();
    }

    SECTION("binding miss")
    {
        tester.set_input("ab");
        tester.set_expected_output("ABCb");
        tester.run();
    }

    SECTION("ambiguous timeout")
    {
        tester.set_simulate_timeouts();
        tester.feed_input("a");
        tester.set_input("a");
        tester.set_expected_output("ABCABC");
        tester.run();
    }
}

//------------------------------------------------------------------------------
TEST_CASE("keyboard macro : record")
{
    line_editor_tester tester;

    // Record a kbd macro.
    tester.set_input(CTRL_X"(" "ab" CTRL_X")");
    tester.set_expected_output("ab");
    tester.run();

    // Verify number argument with call-last-kbd-macro.
    tester.set_input(/*ALT-2 arg*/ ESC"2" /*call-last-kbd-macro*/ CTRL_X"e");
    tester.set_expected_output("abab");
    tester.run();

    // Verify calling an empty kbd macro has no effect.
    tester.set_input(CTRL_X"(" CTRL_X")" CTRL_X"e");
    tester.set_expected_output("");
    tester.run();
}

//------------------------------------------------------------------------------
TEST_CASE("keyboard macro : print")
{
    line_editor_tester tester;

    MAKE_CLEANUP([](){ clink_bind("\\eZ", nullptr, emacs_table); });
    clink_bind("\\eZ", "print-last-kbd-macro", emacs_table);

    tester.set_input(CTRL_X"(" "a" CTRL_Q CTRL_A CTRL_X")" ESC"Z");
    tester.set_expected_output("a" CTRL_A);
    tester.capture_command_output();
    tester.run();

    REQUIRE(strcmp(tester.get_command_output(), "a\\C-q\\C-a\n") == 0);
}

//------------------------------------------------------------------------------
TEST_CASE("operate and get next")
{
    std::unique_ptr<history_list> owned_history;
    if (!history_list::get())
        owned_history = std::make_unique<history_list>();

    line_editor_tester tester;
    line_editor* editor = tester.get_editor();
    REQUIRE(editor->update());
    clear_history();
    MAKE_CLEANUP([]() { clear_history(); });
    history_list* list = history_list::get();
    list->add_history("one");
    list->add_history("two");
    list->add_history("three");
    list->reset_cursor();

    tester.feed_input(CTRL_P CTRL_P); // Recall the second entry.
    REQUIRE(g_tib->get_text() == "two");

    const char* expected = "three";
    int32 expected_index = 2;
    bool pending = true;
    SECTION("next entry") {}
    SECTION("edited entry") { tester.feed_input(" edited"); }
    SECTION("search result")
    {
        tester.feed_input(CTRL_N CTRL_N CTRL_U "tw" ESC"[5~"); // Ctrl+U, prefix, history search.
        REQUIRE(g_tib->get_text() == "two");
    }
    SECTION("explicit entry")
    {
        g_tib->set_numeric_argument(2);
        expected = "two";
        expected_index = 1;
    }
    SECTION("zero argument")
    {
        g_tib->set_numeric_argument(0);
        expected = "one";
        expected_index = 0;
    }
    SECTION("negative argument")
    {
        g_tib->set_numeric_argument(-1);
        expected = "";
        pending = false;
    }
    SECTION("past end")
    {
        g_tib->set_numeric_argument(999);
        expected = "";
        expected_index = 3;
    }
    SECTION("last entry")
    {
        tester.feed_input(CTRL_N);
        expected = "";
        expected_index = 3;
    }
    SECTION("new line")
    {
        tester.feed_input(CTRL_N CTRL_N);
        expected = "";
        expected_index = 3;
    }
    SECTION("empty history")
    {
        clear_history();
        expected = "";
        expected_index = 0;
    }

    str_moveable accepted(g_tib->get_text().c_str());
    auto binding = g_tib->lookup_keyseq(CTRL_O);
    REQUIRE(binding.is_func_name("operate-and-get-next"));
    REQUIRE(g_tib->dispatch(binding.sequence, binding.key, &binding.binding_target, &binding) == 0);
    REQUIRE(g_tib->is_done());
    REQUIRE(has_saved_history() == pending);
    str<> line;
    REQUIRE(editor->get_line(line));
    REQUIRE(line.equals(accepted.c_str()));

    // Starting the next prompt consumes the saved position exactly once.
    REQUIRE(editor->update());
    REQUIRE(!has_saved_history());
    REQUIRE(g_tib->get_text() == expected);
    REQUIRE(g_tib->get_caret() == g_tib->get_length());
    if (pending)
        REQUIRE(list->get_index() == expected_index);
}

//------------------------------------------------------------------------------
TEST_CASE("operate and get next replay")
{
    std::unique_ptr<history_list> owned_history;
    if (!history_list::get())
        owned_history = std::make_unique<history_list>();

    clear_history();
    MAKE_CLEANUP([]() { clear_history(); });

    history_list* list = history_list::get();
    list->add_history("one");
    list->add_history("two");
    list->add_history("three");
    list->reset_cursor();

    line_editor_tester tester;
    line_editor* editor = tester.get_editor();

    REQUIRE(editor->update());
    tester.feed_input(CTRL_P CTRL_P CTRL_P);

    for (const char* expected : { "one", "two", "three" })
    {
        REQUIRE(g_tib->get_text() == expected);
        auto binding = g_tib->lookup_keyseq(CTRL_O);
        REQUIRE(g_tib->dispatch(binding.sequence, binding.key, &binding.binding_target, &binding) == 0);
        str<> line;
        REQUIRE(editor->get_line(line));
        REQUIRE(line.equals(expected));
        REQUIRE(has_saved_history());
        REQUIRE(editor->update());
        REQUIRE(!has_saved_history());
    }

    REQUIRE(g_tib->get_text().empty());
    REQUIRE(list->get_index() == list->length());
    REQUIRE(list->length() == 3);
}
