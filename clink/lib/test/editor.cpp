// Copyright (c) 2020 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include "line_editor_tester.h"
#include "editor_module.h"
#include "line_state.h"
#include "word_collector.h"
#include "rl_integration.h"

#include <core/array.h>

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

TEST_CASE("keyboard macro recording")
{
    line_editor_tester tester;
    auto& box = *g_tib;

    auto command = [&](const char* sequence, const char* name)
    {
        const auto target = tib::binding_target_func(name);
        REQUIRE(box.dispatch(tib::cstring(sequence), 0, &target, nullptr) == 0);
    };
    auto insert = [&](char c)
    {
        const char sequence[] = { c, 0 };
        REQUIRE(box.dispatch(tib::cstring(sequence, 1), uint8_t(c), nullptr, nullptr) == 0);
    };

    command("\x18(", "start-kbd-macro");
    insert('a');
    insert('b');
    command("\x18)", "end-kbd-macro");
    REQUIRE(strcmp(box.get_text().c_str(), "ab") == 0);

    box.set_numeric_argument(2);
    command("\x18" "e", "call-last-kbd-macro");
    REQUIRE(tib::term_in() == 'a');
    REQUIRE(tib::term_in() == 'b');
    REQUIRE(tib::term_in() == 'a');
    REQUIRE(tib::term_in() == 'b');
    REQUIRE(!tib::term_has_macro_input());

    command("\x18(", "start-kbd-macro");
    command("\x18)", "end-kbd-macro");
    command("\x18" "e", "call-last-kbd-macro");
    REQUIRE(!tib::term_has_macro_input());
}
