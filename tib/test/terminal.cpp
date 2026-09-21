// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"

TEST_CASE("Terminal output interface")
{
    tib::cstring output;
    test_output_stream stream(output);

    tib::term_out("abc");
    tib::term_out("defghi", 3);
    tib::ding();

    REQUIRE(output == "abcdef");
    REQUIRE(stream.get_ding_count() == 1);
}

static size_t count_crlf(const tib::cstring& output)
{
    size_t count = 0;
    for (const char* p = output.c_str(); (p = strstr(p, "\r\n")) != nullptr; p += 2)
        ++count;
    return count;
}

TEST_CASE("End display preserves input and requires a fresh begin_display")
{
    tib::cstring output;
    test_output_stream stream(output);
    tib::input_box box;
    box.initialize("abc");
    box.set_caret(1);
    box.display();
    output.clear();
    box.end_display_lf();
    REQUIRE(count_crlf(output) == 1);
    REQUIRE(box.get_text() == "abc");
    REQUIRE(box.get_selection_state().get_caret() == 1);
    REQUIRE(!box.done());
    REQUIRE(box.get_extent().y == 0);
    output.clear();
    box.end_display_lf();
    box.force_redisplay();
    box.display();
    REQUIRE(output.empty());
    box.begin_display();
    box.display();
    REQUIRE(strstr(output.c_str(), "abc") != nullptr);
}

TEST_CASE("End display renders pending input changes")
{
    tib::cstring output;
    test_output_stream stream(output);
    for (int previously_displayed = 0; previously_displayed < 2; ++previously_displayed)
    {
        tib::input_box box;
        box.initialize("abc");
        box.set_origin(1, 1);
        if (previously_displayed)
            box.display();
        box.set_caret(3);
        box.insert_text("def");
        output.clear();
        box.end_display_lf();
        REQUIRE(strstr(output.c_str(), "def") != nullptr);
        REQUIRE(count_crlf(output) == 1);
        REQUIRE(box.get_text() == "abcdef");
        REQUIRE(box.get_selection_state().get_caret() == 6);
        REQUIRE(box.get_extent().y == 0);
    }
}

TEST_CASE("End display distinguishes phantom rows from intentional empty rows")
{
    tib::cstring output;
    test_output_stream stream(output);
    for (int fixed = 0; fixed < 2; ++fixed)
    {
        for (int newline = 0; newline < 2; ++newline)
        {
            tib::input_box box;
            box.set_max_width(4);
            box.set_max_height(2);
            box.set_variable_height(!fixed);
            box.initialize(newline ? "abcd\n" : "abcd");
            box.set_origin(1, 1);
            box.display();
            REQUIRE(box.get_extent().y == 2);
            output.clear();
            box.end_display_lf();
            // The caret already occupies the phantom row; intentional
            // empty rows still need a final line break.
            REQUIRE(count_crlf(output) == size_t(fixed || newline));
        }
    }
}

TEST_CASE("End display discounts only a visible final phantom row")
{
    tib::cstring output;
    test_output_stream stream(output);
    for (int fixed = 0; fixed < 2; ++fixed)
    for (int bordered = 0; bordered < 2; ++bordered)
    for (int additional = 0; additional < 2; ++additional)
    for (int at_end = 0; at_end < 2; ++at_end)
    {
        tib::input_box box;
        box.set_max_width(8);
        box.set_max_height(2);
        box.set_variable_height(!fixed);
        if (bordered)
            box.set_border(&tib::c_light_border);
        box.initialize("abcdefghijklmnopqrstuvwx");
        box.set_caret(at_end ? 24 : 0);
        box.set_origin(1, 1);
        if (additional)
        {
            tib::additional_display_line line;
            line.text.set("status");
            line.width = 6;
            box.set_additional_lines({ line });
        }
        box.display();
        const bool phantom = !fixed && !bordered && !additional && at_end;
        const int32_t line_breaks = box.get_extent().y -
            box.get_relative_cursor().y - int(phantom);
        output.clear();
        box.end_display_lf();
        REQUIRE(count_crlf(output) == size_t(line_breaks));
    }
}

TEST_CASE("End display erases additional rows when the host clears them")
{
    tib::cstring output;
    test_output_stream stream(output);
    tib::input_box box;
    box.set_max_width(8);
    box.set_border(&tib::c_light_border);
    box.initialize("abc");
    box.set_origin(1, 1);
    tib::additional_display_line bounded;
    bounded.text.set("hint");
    bounded.width = 4;
    tib::additional_display_line unbounded;
    unbounded.text.set("status");
    unbounded.width = 6;
    unbounded.bounded = false;
    box.set_additional_lines({ bounded, unbounded });
    box.display();
    REQUIRE(box.get_extent().y == 5);
    output.clear();
    box.clear_additional_lines();
    box.end_display_lf();
    REQUIRE(strstr(output.c_str(), "\x1b[K") != nullptr);
    REQUIRE(strstr(output.c_str(), "\x1b[2;1H") != nullptr);
    // Redisplay restores the caret on row 2 before finishing the box.
    REQUIRE(count_crlf(output) >= 1);
    REQUIRE(strstr(output.c_str(), "abc") == nullptr);
    box.begin_display();
    box.set_origin(1, 8);
    box.display();
    REQUIRE(box.get_extent().y == 3);
}

TEST_CASE("End display avoids an extra newline for full width input")
{
    tib::cstring output;
    test_output_stream stream(output);
    const auto size = tib::get_terminal_size();
    for (int wide = 0; wide < 2; ++wide)
    {
        tib::cstring text;
        text.append_spaces(size.x - (wide ? 2 : 1));
        text.append(wide ? "\xe7\x95\x8c" : "e\xcc\x81");
        tib::input_box box;
        box.set_max_width(size.x);
        box.set_max_height(3);
        box.set_variable_height(true);
        box.initialize(text.c_str());
        box.display();
        output.clear();
        box.end_display_lf();
        REQUIRE(count_crlf(output) == 0);
        REQUIRE(strstr(output.c_str(), "\x1b[K") == nullptr);
    }
}

TEST_CASE("End display finishes the visible scrolled viewport")
{
    tib::cstring output;
    test_output_stream stream(output);
    tib::input_box box;
    box.set_max_width(4);
    box.set_max_height(2);
    box.set_variable_height(true);
    box.initialize("abcdefghijkl");
    box.set_origin(1, 1);
    box.display();
    REQUIRE(box.get_top() > 0);
    output.clear();
    box.end_display_lf();
    REQUIRE(strstr(output.c_str(), "abcd") == nullptr);
    REQUIRE(count_crlf(output) == 0);
}

TEST_CASE("End display retains a full width bottom border")
{
    tib::cstring output;
    test_output_stream stream(output);
    tib::input_box box;
    box.set_max_width(tib::get_terminal_size().x);
    box.set_border(&tib::c_light_border);
    box.initialize("abc");
    box.display();
    output.clear();
    box.end_display_lf();
    REQUIRE(strstr(output.c_str(), tib::c_light_border.bottom_right) == nullptr);
    REQUIRE(strstr(output.c_str(), "\x1b[K") == nullptr);
    REQUIRE(strstr(output.c_str(), "abc") == nullptr);
}

TEST_CASE("End display preserves host additional lines")
{
    tib::cstring output;
    test_output_stream stream(output);
    tib::input_box box;
    box.initialize("abc");
    tib::additional_display_line line;
    line.text.set("status");
    line.width = 6;
    box.set_additional_lines({ line });
    box.end_display_lf();
    REQUIRE(strstr(output.c_str(), "status") != nullptr);
    box.begin_display();
    output.clear();
    box.display();
    REQUIRE(strstr(output.c_str(), "status") != nullptr);
    REQUIRE(box.get_extent().y == 2);
}

namespace {
tib::cstring s_shutdown_events;

class shutdown_test_input final : public tib::terminal_in
{
public:
    ~shutdown_test_input() override { s_shutdown_events.append("I"); }
    bool enable_mouse_input(tib::mouse_input_mode mode, bool) noexcept override
    {
        if (mode == tib::mouse_input_mode::none)
            s_shutdown_events.append("M");
        return true;
    }
    int32_t read() noexcept override { return -1; }
    bool avail(uint32_t) noexcept override { return false; }
};

class shutdown_test_output final : public tib::terminal_out
{
public:
    ~shutdown_test_output() override { s_shutdown_events.append("O"); }
    void write(const char* s, size_t len) noexcept override { s_shutdown_events.append(s, len); }
    void ding() noexcept override {}
};

tib::terminal_in* new_shutdown_test_input(tib::pushed_input&) { return new shutdown_test_input; }
tib::terminal_out* new_shutdown_test_output() { return new shutdown_test_output; }
}

TEST_CASE("Terminal shutdown runs before interface destruction and only on the final end")
{
    const auto input_hook = tib::hook_new_terminal_in;
    const auto output_hook = tib::hook_new_terminal_out;
    tib::term_end();
    tib::hook_new_terminal_in = new_shutdown_test_input;
    tib::hook_new_terminal_out = new_shutdown_test_output;
    s_shutdown_events.clear();
    tib::term_begin();
    const bool mouse_enabled = tib::enable_mouse_input(tib::mouse_input_mode::DRAG);
    tib::term_begin();
    tib::term_end();
    const bool deferred = s_shutdown_events.empty();
    tib::term_end();
    const bool ordered = s_shutdown_events == "\x1b[?25h\x1b[mMIO";
    tib::term_out("unexpected");
    const bool detached = s_shutdown_events == "\x1b[?25h\x1b[mMIO";
    tib::hook_new_terminal_in = input_hook;
    tib::hook_new_terminal_out = output_hook;
    tib::term_begin();
    REQUIRE(mouse_enabled);
    REQUIRE(deferred);
    REQUIRE(ordered);
    REQUIRE(detached);
}
