// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h"

#include "line_editor_tester.h"
#include "line_editor_integration.h"
#include "char_reader_impl.h"
#include "rl_integration.h"

#include <core/settings.h>
#include <terminal/terminal.h>
#include <tib.h>
#include <vector>

extern "C" {
#include <readline/history.h>
}

class read_start_module : public editor_module
{
public:
    void start(char_reader_callback callback)
    {
        m_callback = std::move(callback);
    }
    void start_number(read_number_callback callback, read_number_change_callback on_change, int32 max_number)
    {
        m_number_callback = std::move(callback);
        m_number_change = std::move(on_change);
        m_max_number = max_number;
    }
    bool activated() const { return m_activated; }

private:
    void bind_input(binder& binder) override { binder.bind(binder.get_group(), "^X", 1); }
    void on_begin_line(const context& context) override {}
    void on_end_line() override {}
    void on_need_input(int32& bind_group) override {}
    void on_input(const input& input, result& result, const context& context) override
    {
        if (m_number_callback)
            m_activated = read_number(result, std::move(m_number_callback),
                                      std::move(m_number_change), m_max_number);
        else
            m_activated = read_one_codepoint(result, std::move(m_callback));
    }
    void on_matches_changed(const context& context, const line_state& line, const char* needle) override {}
    void on_terminal_resize(int32 columns, int32 rows, const context& context) override {}
    void on_signal(int32 sig) override {}

    char_reader_callback m_callback;
    read_number_callback m_number_callback;
    read_number_change_callback m_number_change;
    int32 m_max_number = 999999;
    bool m_activated = false;
};

TEST_CASE("read number")
{
    line_editor_tester tester;
    read_start_module start;
    line_editor* editor = tester.get_editor();
    REQUIRE(editor->add_module(start));
    REQUIRE(editor->update());

    read_number_result value;
    std::vector<int32> changes;
    int calls = 0;
    auto callback = [&](read_number_result result) { value = result; ++calls; };
    auto on_change = [&](int32 number) { changes.push_back(number); };

    start.start_number(callback, on_change, 25);
    tester.feed_input("\x18");
    REQUIRE(start.activated());
    REQUIRE(is_read_number_active());
    const std::vector<int32> initial{ -1 };
    REQUIRE(changes == initial);
    tester.feed_input("12\x08" "39"); // 9 would exceed the limit.
    const std::vector<int32> edited{ -1, 1, 12, 1, 13, 13 };
    REQUIRE(changes == edited);
    tester.feed_input("x\x01"); // Non-digits and control keys ding.
    REQUIRE(is_read_number_active());
    REQUIRE(calls == 0);
    REQUIRE(changes == edited);
    tester.feed_input("\r");
    REQUIRE(!is_read_number_active());
    REQUIRE(calls == 1);
    REQUIRE(value.success);
    REQUIRE(value.number == 13);

    start.start_number(callback, on_change, 25);
    tester.feed_input("\x18" "7\x07");
    REQUIRE(calls == 2);
    REQUIRE(!value.success);
    REQUIRE(value.number == 7);
}

TEST_CASE("read codepoint")
{
    line_editor_tester tester;
    read_start_module start;
    line_editor* editor = tester.get_editor();
    REQUIRE(editor->add_module(start));
    REQUIRE(editor->update());

    char_reader_result result;
    int calls = 0;
    auto callback = [&](char_reader_result _result) { result = std::move(_result); ++calls; };

    start.start(callback);
    tester.feed_input("\x18");
    REQUIRE(start.activated());

    SECTION("ASCII")
    {
        tester.feed_input("x");
        REQUIRE(calls == 1);
        REQUIRE(result.success);
        REQUIRE(strcmp(result.utf8.c_str(), "x") == 0);
        REQUIRE(g_tib->get_text().empty());
    }

    SECTION("UTF-8")
    {
        tester.feed_input("\xf0\x9f\x98\x80");
        REQUIRE(calls == 1);
        REQUIRE(result.success);
        REQUIRE(strcmp(result.utf8.c_str(), "\xf0\x9f\x98\x80") == 0);
    }

    SECTION("two-byte UTF-8")
    {
        tester.feed_input("\xc3\xa9");
        REQUIRE(calls == 1);
        REQUIRE(result.success);
        REQUIRE(strcmp(result.utf8.c_str(), "\xc3\xa9") == 0);
    }

    SECTION("three-byte UTF-8")
    {
        tester.feed_input("\xe2\x82\xac");
        REQUIRE(calls == 1);
        REQUIRE(result.success);
        REQUIRE(strcmp(result.utf8.c_str(), "\xe2\x82\xac") == 0);
    }

    SECTION("invalid UTF-8")
    {
        tester.feed_input("\xf4\x90\x80\x80");
        REQUIRE(calls == 1);
        REQUIRE(!result.success);
        REQUIRE(result.utf8.empty());
    }

    SECTION("another read after callback")
    {
        tester.feed_input("x");
        REQUIRE(calls == 1);
        start.start(callback);
        tester.feed_input("\x18");
        REQUIRE(start.activated());
        tester.feed_input("y");
        REQUIRE(calls == 2);
        REQUIRE(strcmp(result.utf8.c_str(), "y") == 0);
    }
}

TEST_CASE("read codepoint controls")
{
    line_editor_tester tester;
    read_start_module start;
    line_editor* editor = tester.get_editor();
    REQUIRE(editor->add_module(start));
    REQUIRE(editor->update());
    tester.feed_input("a");

    char_reader_result result;
    int calls = 0;
    auto callback = [&](char_reader_result _result) { result = std::move(_result); ++calls; };

    SECTION("control character")
    {
        start.start(callback);
        tester.feed_input("\x18");
        REQUIRE(start.activated());
        tester.feed_input("\x01");
        REQUIRE(calls == 1);
        REQUIRE(result.success);
        REQUIRE(strcmp(result.utf8.c_str(), "\x01") == 0);
        REQUIRE(g_tib->get_text() == "a");
    }

    SECTION("backspace character")
    {
        start.start(callback);
        tester.feed_input("\x18");
        REQUIRE(start.activated());
        tester.feed_input("\x08");
        REQUIRE(calls == 1);
        REQUIRE(result.success);
        REQUIRE(strcmp(result.utf8.c_str(), "\x08") == 0);
        REQUIRE(g_tib->get_text() == "a");
    }

    SECTION("Ctrl+G cancels")
    {
        start.start(callback);
        tester.feed_input("\x18");
        REQUIRE(start.activated());
        tester.feed_input("\x07");
        REQUIRE(calls == 1);
        REQUIRE(!result.success);
        REQUIRE(!is_char_reader_active());
        tester.feed_input("z");
        REQUIRE(g_tib->get_text() == "az");
    }

    SECTION("physical Esc cancels")
    {
        if (const char* esc = get_bindable_esc())
        {
            start.start(callback);
            tester.feed_input("\x18");
            REQUIRE(start.activated());
            tester.feed_input(esc);
            REQUIRE(calls == 1);
            REQUIRE(!result.success);
            REQUIRE(!is_char_reader_active());
            tester.feed_input("z");
            REQUIRE(g_tib->get_text() == "az");
        }
    }

    SECTION("partial UTF-8 then Ctrl+G cancels")
    {
        start.start(callback);
        tester.feed_input("\x18");
        REQUIRE(start.activated());
        tester.feed_input("\xc3\x07");
        REQUIRE(calls == 1);
        REQUIRE(!result.success);
        REQUIRE(!is_char_reader_active());
        tester.feed_input("z");
        REQUIRE(g_tib->get_text() == "az");
    }
}

TEST_CASE("win copy up to char")
{
    setting* popup_color = settings::find("color.popup");
    REQUIRE(popup_color);
    str_moveable previous_popup_color;
    popup_color->get(previous_popup_color);
    REQUIRE(popup_color->set("cyan on black"));
    MAKE_CLEANUP([&]() { popup_color->set(previous_popup_color.c_str()); });

    line_editor_tester tester;
    REQUIRE(tester.get_editor()->update());

    MAKE_CLEANUP([]() {
        clink_bind("\\eZ", nullptr, emacs_table);
        clear_history();
    });
    clear_history();
    add_history("ab\xc3\xa9XYZ");
    history_set_pos(history_length);
    clink_bind("\\eZ", "win-copy-up-to-char", emacs_table);

    tester.feed_input("abZZ\x1b[D\x1b[D");
    REQUIRE(g_tib->get_caret() == 2);
    tester.feed_input("\x1bZ");
    REQUIRE(g_tib->get_text() == "abZZ");
    REQUIRE(is_char_reader_active());
    tester.feed_input("X");
    REQUIRE(!is_char_reader_active());
    REQUIRE(g_tib->get_text() == "ab\xc3\xa9Z");
    REQUIRE(g_tib->get_caret() == 4);

    tester.feed_input("\x1bZ");
    tester.feed_input("\x07"); // Ctrl+G cancels.
    REQUIRE(!is_char_reader_active());
    REQUIRE(g_tib->get_text() == "ab\xc3\xa9Z");
    tester.feed_input("Q");
    REQUIRE(g_tib->get_text() == "ab\xc3\xa9QZ");
}

TEST_CASE("win copy up to char with suggestion list")
{
    setting* popup_color = settings::find("color.popup");
    REQUIRE(popup_color);
    str_moveable previous_popup_color;
    popup_color->get(previous_popup_color);
    REQUIRE(popup_color->set("cyan on black"));
    MAKE_CLEANUP([&]() { popup_color->set(previous_popup_color.c_str()); });

    setting* autosuggest = settings::find("autosuggest.enable");
    REQUIRE(autosuggest);
    str_moveable previous_setting;
    autosuggest->get(previous_setting);
    REQUIRE(autosuggest->set("true"));
    MAKE_CLEANUP([&]() { autosuggest->set(previous_setting.c_str()); });

    line_editor_tester tester;
    REQUIRE(tester.get_editor()->update());

    MAKE_CLEANUP([]() {
        clink_bind("\\eZ", nullptr, emacs_table);
        clear_history();
    });
    clear_history();
    add_history("ab\xc3\xa9XYZ");
    history_set_pos(history_length);
    clink_bind("\\eZ", "win-copy-up-to-char", emacs_table);

    tester.feed_input("abZZ\x1b[D\x1b[D");
    tester.feed_input("\x1bOQ"); // Activate the suggestion list.
    REQUIRE(is_suggestion_list_active(true));
    tester.feed_input("\x1bZ");
    REQUIRE(g_tib->get_text() == "abZZ");
    REQUIRE(is_char_reader_active());
    tester.feed_input("X");
    REQUIRE(!is_char_reader_active());
    REQUIRE(g_tib->get_text() == "ab\xc3\xa9Z");
}

TEST_CASE("win delete up to char")
{
    setting* popup_color = settings::find("color.popup");
    REQUIRE(popup_color);
    str_moveable previous_popup_color;
    popup_color->get(previous_popup_color);
    REQUIRE(popup_color->set("cyan on black"));
    MAKE_CLEANUP([&]() { popup_color->set(previous_popup_color.c_str()); });

    line_editor_tester tester;
    REQUIRE(tester.get_editor()->update());

    tester.feed_input("abX\xc3\xa9Z\x1b[D\x1b[D\x1b[D");
    REQUIRE(g_tib->get_caret() == 2);

    tester.feed_input("\x1bOS"); // F4.
    REQUIRE(is_char_reader_active());
    tester.feed_input("\xc3\xa9");
    REQUIRE(!is_char_reader_active());
    REQUIRE(g_tib->get_text() == "ab\xc3\xa9Z");
    REQUIRE(g_tib->get_caret() == 2);

    tester.feed_input("\x1bOS");
    tester.feed_input("Q"); // No match deletes through the end of the line.
    REQUIRE(g_tib->get_text() == "ab");

    tester.feed_input("cd\x1b[D\x1b[D");
    tester.feed_input("\x1bOS");
    REQUIRE(!g_terminal->is_bound("\x1b[D", 3)); // Two Left keys are both filtered.
    REQUIRE(!g_terminal->is_bound("\x1b[D", 3));
    tester.feed_input("\x07"); // Ctrl+G cancels.
    REQUIRE(!is_char_reader_active());
    REQUIRE(g_tib->get_text() == "abcd");
    tester.feed_input("Q");
    REQUIRE(g_tib->get_text() == "abQcd");
}

TEST_CASE("win copy history number")
{
    setting* autosuggest = settings::find("autosuggest.enable");
    REQUIRE(autosuggest);
    str_moveable previous_autosuggest;
    autosuggest->get(previous_autosuggest);
    REQUIRE(autosuggest->set("true"));
    MAKE_CLEANUP([&]() { autosuggest->set(previous_autosuggest.c_str()); });

    setting* popup_color = settings::find("color.popup");
    REQUIRE(popup_color);
    str_moveable previous_popup_color;
    popup_color->get(previous_popup_color);
    REQUIRE(popup_color->set("cyan on black"));
    MAKE_CLEANUP([&]() { popup_color->set(previous_popup_color.c_str()); });

    line_editor_tester tester;
    REQUIRE(tester.get_editor()->update());

    MAKE_CLEANUP([]() { clear_history(); });
    clear_history();
    for (int i = 1; i <= 12; ++i)
    {
        str_moveable entry;
        entry.format("history %d", i);
        add_history(entry.c_str());
    }
    history_set_pos(history_length);

    tester.feed_input("draft");
    if (!is_suggestion_list_active(true))
        tester.feed_input("\x1bOQ"); // Activate the suggestion list.
    REQUIRE(is_suggestion_list_active(true));
    tester.feed_input("\x1b[20~"); // F9.
    REQUIRE(is_read_number_active());
    REQUIRE(g_tib->get_text() == "draft");
    tester.feed_input("12\x08" "2\r");
    REQUIRE(!is_read_number_active());
    REQUIRE(g_tib->get_text() == "history 12");
    REQUIRE(g_tib->get_caret() == g_tib->get_length());

    tester.feed_input("\x1b[20~2");
    REQUIRE(is_read_number_active());
    if (const char* esc = get_bindable_esc())
        tester.feed_input(esc);
    else
        tester.feed_input("\x1b");
    REQUIRE(!is_read_number_active());
    REQUIRE(g_tib->get_text() == "history 12");

    tester.feed_input("\x1b[20~1\r");
    REQUIRE(g_tib->get_text() == "history 1");
    tester.feed_input("\x1b[20~99\r"); // Out of range selects the last entry.
    REQUIRE(g_tib->get_text() == "history 12");
}
