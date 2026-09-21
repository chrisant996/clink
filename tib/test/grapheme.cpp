// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "test.h"
#include "test_util.h"
#include "tib.h"
#include "wcwidth.h"

constexpr uint32_t c_display_line_comparison_passes = 100000;

struct grapheme_sample
{
    const char*         text;
    uint16_t            width;
};

static void check_back_one_grapheme(const grapheme_sample* samples, size_t count)
{
    tib::cstring text;
    std::vector<uint32_t> positions;
    for (size_t i = 0; i < count; ++i)
    {
        positions.emplace_back(uint32_t(text.length()));
        text.append(samples[i].text);
    }

    uint32_t caret = uint32_t(text.length());
    while (count)
    {
        --count;
        uint16_t width = 0;
        caret = tib::backward_one_grapheme(text.c_str(), text.length(), caret, &width);
        REQUIRE(caret == positions[count]);
        REQUIRE(width == samples[count].width, [&](){
            tib::cstring_t<WCHAR> ws;
            to_utf16(samples[count].text, tib::c_auto_length, ws);
            tib::cstring_t<WCHAR> msg;
            msg.printf(L"index     %u\ngrapheme  '%s'\nexpected  %u\nwidth     %u",
                       count, ws.c_str(), samples[count].width, width);
            DWORD written;
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), msg.c_str(), DWORD(msg.length()), &written, nullptr);
        });
    }

    uint16_t width = 1;
    REQUIRE(tib::backward_one_grapheme(text.c_str(), text.length(), caret, &width) == 0);
    REQUIRE(width == 0);
}

TEST_CASE("Back one grapheme")
{
    SECTION("ASCII and UTF8 encodings")
    {
        const grapheme_sample samples[] =
        {
            { "a", 1 },
            { "Z", 1 },
            { "\xc2\xa2", 1 },                 // U+00A2 CENT SIGN; 2-byte UTF8.
            { "\xe4\xb8\xad", 2 },             // U+4E2D CJK character; 3-byte UTF8.
            { "\xf0\x9f\x98\x80", 2 },         // U+1F600 GRINNING FACE; 4-byte UTF8.
        };
        check_back_one_grapheme(samples, std::size(samples));
    }

    SECTION("Combining marks")
    {
        const grapheme_sample samples[] =
        {
            { "a\xcc\x81", 1 },                 // a + U+0301 COMBINING ACUTE ACCENT.
            { "o\xcc\x82\xcc\x88", 1 },         // o + circumflex + diaeresis.
            { "u\xcc\x88\xcc\x84", 1 },         // u + diaeresis + macron.
        };
        check_back_one_grapheme(samples, std::size(samples));
    }

    SECTION("Emoji and variation selectors")
    {
        const grapheme_sample samples[] =
        {
            { "\xf0\x9f\x98\x80", 2 },                         // Grinning face.
            { "\xf0\x9f\x9a\x80", 2 },                         // Rocket.
            { "\xe2\x9d\xa4\xef\xb8\x8f", 2 },                 // Heart ending in U+FE0F.
            { "\xe2\x9c\x88\xef\xb8\x8f", 2 },                 // Airplane ending in U+FE0F.
            { "\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d"
              "\xf0\x9f\x94\xa5", 2 },                         // Flaming heart; contains U+FE0F.
            { "\xf0\x9f\x91\xa8\xe2\x80\x8d"
              "\xf0\x9f\x92\xbb", 2 },                         // Man technologist.
            { "\xf0\x9f\x99\x86\xe2\x80\x8d"
              "\xe2\x99\x80\xef\xb8\x8f", 2 },                 // Woman gesturing OK.
            { "\xf0\x9f\x91\xa8\xe2\x80\x8d"
              "\xe2\x9a\x95\xef\xb8\x8f", 2 },                 // Man health worker.
        };
        check_back_one_grapheme(samples, std::size(samples));
    }

    SECTION("Flag sequences")
    {
        const grapheme_sample samples[] =
        {
            { "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8", 2 },         // United States.
            { "\xf0\x9f\x87\xaf\xf0\x9f\x87\xb5", 2 },         // Japan.
            { "\xf0\x9f\x87\xa8\xf0\x9f\x87\xa6", 2 },         // Canada.
        };
        check_back_one_grapheme(samples, std::size(samples));
    }
}

class display_test_buffer : public tib::input_buffer
{
public:
    void set_text(const char* text, tib::textpos_t caret, tib::textpos_t anchor=-1)
    {
        m_text.set(text);
        if (anchor < 0)
            m_selection.set_caret(caret);
        else
            m_selection.set_selection(anchor, caret);
        ++m_change_counter;
    }

    void set_selection(tib::textpos_t anchor, tib::textpos_t caret)
    {
        m_selection.set_selection(anchor, caret);
    }

    tib::selection_state& get_selection_state_out()
    {
        return m_selection;
    }
};

class display_test_callbacks : public tib::editor_callbacks
{
public:
    void provide_faces(const tib::input_buffer&, tib::cstring&) override
    {
        ++m_calls;
    }

    uint32_t m_calls = 0;
};

static tib::cstring s_display_output;

class display_test_fixture
{
public:
    display_test_fixture(uint16_t max_width=10, bool horiz_scroll_markers=false,
                         uint16_t max_height=1, bool variable_height=false,
                         const tib::border_definition* border=nullptr,
                         int32_t origin_x=1)
    : m_output(s_display_output)
    {
        m_old_coalesce = tib::g_coalesce_output;
        tib::g_coalesce_output = true;

        m_layout.max_width = max_width;
        m_layout.max_height = max_height;
        m_layout.variable_height = variable_height;
        m_style.border = border ? border : &m_border;
        m_style.horiz_scroll_markers = horiz_scroll_markers;
        m_display.init_layout(&m_layout);
        m_display.init_buffer(&m_buffer);
        m_display.init_style(&m_style);
        m_display.set_origin(origin_x, 1);
    }

    ~display_test_fixture()
    {
        tib::g_coalesce_output = m_old_coalesce;
        s_display_output.clear();
    }

    bool display_initial(const char* text, tib::textpos_t caret, tib::textpos_t anchor=-1)
    {
        m_buffer.set_text(text, caret, anchor);
        const bool any_updates = m_display.display();
        s_display_output.clear();
        return any_updates;
    }

    display_test_buffer m_buffer;
    tib::display_manager m_display;

private:
    test_output_stream  m_output;
    tib::layout_info m_layout;
    tib::border_definition m_border;
    tib::style_info m_style;
    bool m_old_coalesce = false;
};

TEST_CASE("Display differential updates")
{
    SECTION("Invalidation preserves matching display content")
    {
        display_test_fixture fixture;
        REQUIRE(fixture.display_initial("abc", 3) == true);

        fixture.m_display.invalidate();
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(strstr(s_display_output.c_str(), "abc") == nullptr);
    }

    SECTION("Forced redisplay redraws matching display content")
    {
        display_test_fixture fixture;
        REQUIRE(fixture.display_initial("abc", 3) == true);

        fixture.m_display.force_redisplay();
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "abc") != nullptr);
    }

    SECTION("Clears a variable-height line through the full terminal width")
    {
        display_test_fixture fixture(tib::int16_max, false, 3, true);
        fixture.m_buffer.set_text("abc\ndef", 7);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[K") != nullptr);
    }

    SECTION("Clears a changed bounded line without entering pending wrap")
    {
        tib::border_definition border;
        border.top = "-";
        border.bottom = "-";
        border.left = "||";
        border.right = "||";
        border.top_width = 1;
        border.bottom_width = 1;
        border.left_width = 2;
        border.right_width = 2;
        display_test_fixture fixture(tib::int16_max, false, 3, true, &border, 5);
        REQUIRE(fixture.display_initial("abc", 3) == true);

        tib::additional_display_line line;
        line.text.set("keys: a");
        line.width = 7;
        line.bounded = true;
        fixture.m_display.set_additional_lines({ line });
        REQUIRE(fixture.m_display.display() == true);

        line.text.set("keys: b");
        fixture.m_display.set_additional_lines({ line });
        s_display_output.clear();
        REQUIRE(fixture.m_display.display() == true);
        const char* const changed_line = strstr(s_display_output.c_str(), "keys: b");
        REQUIRE(changed_line != nullptr);
        REQUIRE(strstr(changed_line, "\x1b[K") != nullptr);
    }

    SECTION("Redraws changed left text")
    {
        display_test_fixture fixture;
        fixture.m_display.set_left_text("old: ", 5);
        REQUIRE(fixture.display_initial("abc", 0) == true);

        fixture.m_display.set_left_text("new: ", 5);
        REQUIRE(fixture.m_display.display() == true);
        // A changed left text is printed from the beginning of the row, and
        // the input follows it without an intervening column move.
        REQUIRE(strstr(s_display_output.c_str(), "\rnew: ") != nullptr);
    }

    SECTION("Does not redraw unchanged left text during minimal input updates")
    {
        display_test_fixture fixture(20);
        fixture.m_display.set_left_text("left: ", 6);
        REQUIRE(fixture.display_initial("abcde", 5) == true);

        // A change at the beginning exercises the matching-suffix
        // optimization while leaving begin at zero.
        fixture.m_buffer.set_text("Xbcde", 5);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[7G") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "X") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "left: ") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "bcde") == nullptr);

        // A change at the end exercises the matching-prefix optimization.
        s_display_output.clear();
        fixture.m_buffer.set_text("XbcdY", 5);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[11G") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "Y") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "left: ") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "Xbcd") == nullptr);
    }

    SECTION("Does not redraw unchanged message text during minimal input updates")
    {
        display_test_fixture fixture(20);
        fixture.m_display.set_message_text("message: ", 9);
        REQUIRE(fixture.display_initial("abcde", 5) == true);

        // A change at the beginning exercises the matching-suffix
        // optimization while leaving begin at zero.
        fixture.m_buffer.set_text("Xbcde", 5);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[10G") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "X") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "message: ") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "bcde") == nullptr);

        // A change at the end exercises the matching-prefix optimization.
        s_display_output.clear();
        fixture.m_buffer.set_text("XbcdY", 5);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[14G") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "Y") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "message: ") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "Xbcd") == nullptr);
    }

    SECTION("Reuses a line only when all displayed text matches")
    {
        display_test_fixture fixture;
        fixture.m_display.set_right_text("xyz", 3);
        REQUIRE(fixture.display_initial("abc", 0) == true);

        // Moving the caret forces a rebuild with an unchanged line.  Reusing
        // the displayed line must avoid outputting either part of that line.
        fixture.m_buffer.set_selection(1, 1);
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(strpbrk(s_display_output.c_str(), "abc") == nullptr);
        REQUIRE(strpbrk(s_display_output.c_str(), "xyz") == nullptr);

        // Right text is part of the first displayed line, so changing it
        // must prevent reuse even though the input text is unchanged.
        s_display_output.clear();
        fixture.m_display.set_right_text("zzz", 3);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "zzz") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "xyz") == nullptr);
    }

    SECTION("Displays right text when a scrolled line becomes short enough")
    {
        display_test_fixture fixture(10, false, 2);
        fixture.m_display.set_right_text("xyz", 3);
        REQUIRE(fixture.display_initial("first\n1234567890\nlast", 21) == true);

        fixture.m_buffer.set_text("first\n123\nlast", 14);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "xyz") != nullptr);
    }

    SECTION("Caret-only updates skip rebuilding display rows")
    {
        display_test_fixture fixture(5, false, 3);
        display_test_callbacks callbacks;
        fixture.m_display.init_callbacks(&callbacks);
        REQUIRE(fixture.display_initial("abcde12345", 1) == true);
        REQUIRE(callbacks.m_calls == 1);

        fixture.m_buffer.set_selection(7, 7);
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(callbacks.m_calls == 1);
        const tib::coord expected = { 2, 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
        REQUIRE(strstr(s_display_output.c_str(), "abcde") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "12345") == nullptr);
    }

    SECTION("Caret-only updates preserve grapheme columns")
    {
        display_test_fixture fixture(10, false, 2);
        display_test_callbacks callbacks;
        fixture.m_display.init_callbacks(&callbacks);
        REQUIRE(fixture.display_initial("a\xe4\xb8\xad" "b", 0) == true);
        REQUIRE(callbacks.m_calls == 1);

        fixture.m_buffer.set_selection(4, 4);
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(callbacks.m_calls == 1);
        const tib::coord expected = { 3, 0 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }

    SECTION("Caret-only updates handle split controls and phantom rows")
    {
        display_test_fixture fixture(8, false, 3);
        display_test_callbacks callbacks;
        fixture.m_display.init_callbacks(&callbacks);
        REQUIRE(fixture.display_initial("aaaaaaa\t", 8) == true);
        REQUIRE(callbacks.m_calls == 1);

        fixture.m_buffer.set_selection(7, 7);
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(callbacks.m_calls == 1);
        const tib::coord before_control = { 7, 0 };
        REQUIRE(fixture.m_display.get_relative_cursor() == before_control);

        fixture.m_buffer.set_text("abcdefgh", 0);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(callbacks.m_calls == 2);
        fixture.m_buffer.set_selection(8, 8);
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(callbacks.m_calls == 2);
        const tib::coord phantom_row = { 0, 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == phantom_row);
    }

    SECTION("Padding rows do not masquerade as end-of-input rows")
    {
        display_test_fixture fixture(8, false, 3);
        display_test_callbacks callbacks;
        fixture.m_display.init_callbacks(&callbacks);
        REQUIRE(fixture.display_initial("abc", 0) == true);
        REQUIRE(callbacks.m_calls == 1);

        fixture.m_buffer.set_selection(3, 3);
        REQUIRE(fixture.m_display.display() == false);
        REQUIRE(callbacks.m_calls == 1);
        const tib::coord expected = { 3, 0 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }

    SECTION("Caret-only updates fall back when the viewport must scroll")
    {
        display_test_fixture fixture(5, false, 2);
        display_test_callbacks callbacks;
        fixture.m_display.init_callbacks(&callbacks);
        REQUIRE(fixture.display_initial("abcdefghijklmno", 1) == true);
        REQUIRE(callbacks.m_calls == 1);

        fixture.m_buffer.set_selection(12, 12);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(callbacks.m_calls == 2);
        const uint32_t expected_top = (tib::c_horz_scroll_indicator_chars == 3) ? 2 : 1;
        REQUIRE(fixture.m_display.get_top() == expected_top);
        const tib::coord expected = { 2, (tib::c_horz_scroll_indicator_chars == 3) ? 0 : 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }

    SECTION("Selection changes still rebuild display rows")
    {
        display_test_fixture fixture;
        display_test_callbacks callbacks;
        fixture.m_display.init_callbacks(&callbacks);
        REQUIRE(fixture.display_initial("abc", 0) == true);
        REQUIRE(callbacks.m_calls == 1);

        fixture.m_buffer.set_selection(0, 1);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(callbacks.m_calls == 2);
    }

    SECTION("Skips matching leading and trailing text")
    {
        display_test_fixture fixture;
        REQUIRE(fixture.display_initial("abcde", 5) == true);

        fixture.m_buffer.set_text("abXde", 5);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "X") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "ab") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "de") == nullptr);
    }

    SECTION("Compares faces along with text")
    {
        display_test_fixture fixture;
        REQUIRE(fixture.display_initial("abc", 0) == true);

        fixture.m_buffer.set_selection(1, 2);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "b") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "a") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "c") == nullptr);
    }

    SECTION("Does not split matching grapheme prefixes")
    {
        display_test_fixture fixture;
        REQUIRE(fixture.display_initial("a\xcc\x81x", 4) == true);

        fixture.m_buffer.set_text("a\xcc\x88x", 4);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "a\xcc\x88") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "x") == nullptr);
    }

    SECTION("Does not split matching grapheme suffixes")
    {
        display_test_fixture fixture;
        REQUIRE(fixture.display_initial("xa\xcc\x81", 4) == true);

        fixture.m_buffer.set_text("xb\xcc\x81", 4);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "b\xcc\x81") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "x") == nullptr);
    }
}

TEST_CASE("Display left text")
{
    SECTION("Participates in wrapping")
    {
        display_test_fixture fixture(5, false, 3, true);
        fixture.m_display.set_left_text("> ", 2);
        fixture.m_buffer.set_text("abcd", 4);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "> ") != nullptr);
        const tib::coord expected = { 1, 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }

    SECTION("Is omitted unless an input column remains")
    {
        display_test_fixture fixture(5, false, 3, true);
        fixture.m_display.set_left_text("12345", 5);
        fixture.m_buffer.set_text("abcd", 4);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "12345") == nullptr);
        const tib::coord expected = { 4, 0 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }

    SECTION("Is omitted when horizontally scrolled")
    {
        display_test_fixture fixture(8, true);
        fixture.m_display.set_left_text("1234567", 7);
        fixture.m_buffer.set_text("abcdefghijkl", 12);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(fixture.m_display.get_left() > 0);
        REQUIRE(strstr(s_display_output.c_str(), "1234567") == nullptr);
    }

    SECTION("Is displayed before horizontal scrolling starts")
    {
        display_test_fixture fixture(8, true);
        fixture.m_display.set_left_text("> ", 2);
        fixture.m_buffer.set_text("abc", 3);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(fixture.m_display.get_left() == 0);
        REQUIRE(strstr(s_display_output.c_str(), "> ") != nullptr);
    }

    SECTION("Is not displayed when the first logical line is scrolled away")
    {
        display_test_fixture fixture(5, false, 2);
        fixture.m_display.set_left_text("> ", 2);
        fixture.m_buffer.set_text("abcdefghijkl", 12);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(fixture.m_display.get_top() > 0);
        REQUIRE(strstr(s_display_output.c_str(), "> ") == nullptr);
    }
}

TEST_CASE("Display suggestion text")
{
    SECTION("Is rendered after input with the suggestion face")
    {
        display_test_fixture fixture(10, false, 3, true);
        tib::face_definitions faces;
        faces[tib::FACE_SUGGESTION] = "35";
        fixture.m_display.init_faces(&faces);
        fixture.m_display.set_suggestion_text("def", 3);
        fixture.m_buffer.set_text("abc", 3);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[35mdef") != nullptr);
        const tib::coord expected_cursor = { 3, 0 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected_cursor);
        REQUIRE(fixture.m_buffer.get_text() == "abc");
    }

    SECTION("Wraps without moving the real caret onto the suggestion row")
    {
        display_test_fixture fixture(5, false, 3, true);
        fixture.m_display.set_suggestion_text("\xe4\xb8\xad", 3);
        fixture.m_buffer.set_text("abcd", 4);

        REQUIRE(fixture.m_display.display() == true);
        const tib::coord expected_cursor = { 0, 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected_cursor);
        const tib::coord expected_extent = { 5, 2 };
        REQUIRE(fixture.m_display.get_inner_extent() == expected_extent);
        REQUIRE(strstr(s_display_output.c_str(), "\xe4\xb8\xad") != nullptr);
    }

    SECTION("Clearing removes suggestion-only height")
    {
        display_test_fixture fixture(5, false, 3, true);
        fixture.m_display.set_suggestion_text("def", 3);
        REQUIRE(fixture.display_initial("abc", 3) == true);
        const tib::coord initial_extent = { 5, 2 };
        REQUIRE(fixture.m_display.get_inner_extent() == initial_extent);

        fixture.m_display.set_suggestion_text(nullptr, 0);
        REQUIRE(fixture.m_display.display() == true);
        const tib::coord final_extent = { 5, 1 };
        REQUIRE(fixture.m_display.get_inner_extent() == final_extent);
    }
}

TEST_CASE("Display usage text")
{
    SECTION("Supersedes right text and uses the final input row")
    {
        display_test_fixture fixture(10, false, 3, true);
        fixture.m_display.set_right_text("right", 5);
        fixture.m_display.set_usage_text("use", 3);
        fixture.m_buffer.set_text("1234567890\nabc", 14);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "use") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "right") == nullptr);
    }

    SECTION("Adds a row when the final input row is too full")
    {
        display_test_fixture fixture(10, false, 3, true);
        fixture.m_display.set_usage_text("use", 3);
        fixture.m_buffer.set_text("12345678", 8);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "use") != nullptr);
        const tib::coord expected_extent = { 10, 2 };
        REQUIRE(fixture.m_display.get_inner_extent() == expected_extent);
    }

    SECTION("Uses a fixed-height padding row")
    {
        display_test_fixture fixture(10, false, 3, false);
        fixture.m_display.set_usage_text("use", 3);
        fixture.m_buffer.set_text("12345678", 8);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "use") != nullptr);
        const tib::coord expected_extent = { 10, 3 };
        REQUIRE(fixture.m_display.get_inner_extent() == expected_extent);
    }

    SECTION("Is omitted at maximum height while still superseding right text")
    {
        display_test_fixture fixture(10);
        fixture.m_display.set_right_text("right", 5);
        fixture.m_display.set_usage_text("use", 3);
        fixture.m_buffer.set_text("12345678", 8);

        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "use") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "right") == nullptr);
    }
}

TEST_CASE("Display vertical caret movement")
{
    display_test_fixture fixture(5, false, 3, true);
    REQUIRE(fixture.display_initial("abc\ndef\nghi", 1, 0) == true);

    const int32_t cursor_column = fixture.m_display.get_relative_cursor().x;
    fixture.m_display.invalidate();
    REQUIRE(fixture.m_display.move_caret_vertically(
                1, cursor_column, fixture.m_buffer.get_selection_state_out(), true/*select*/));
    REQUIRE(fixture.m_buffer.get_selection_state().get_anchor() == 0);
    REQUIRE(fixture.m_buffer.get_selection_state().get_caret() == 5);

    REQUIRE(fixture.m_display.display() == true);
    REQUIRE(fixture.m_display.move_caret_vertically(
                -1, cursor_column, fixture.m_buffer.get_selection_state_out()));
    REQUIRE(fixture.m_buffer.get_selection_state().get_anchor() == 1);
    REQUIRE(fixture.m_buffer.get_selection_state().get_caret() == 1);
}

TEST_CASE("Display horizontal scrolling")
{
    display_test_fixture fixture(40, true);
    const tib::textpos_t expected_left[][3] =
    {
        { 1, 7, 9 },
        { 1, 8, 10 },
        { 7, 9, 11 },
    };
    const int32_t expected_cursor[][3] =
    {
        { 37, 37, 36 },
        { 38, 37, 36 },
        { 38, 37, 36 },
    };
    const size_t marker_index = tib::c_horz_scroll_indicator_chars - 1;
    tib::cstring text;
    text.set("x\xe2\x9c\x94\xef\xb8\x8fy"); // x + U+2714 U+FE0F + y.
    for (uint32_t i = 0; i < 35; ++i)
        text.append("x");

    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) == true);
    REQUIRE(fixture.m_display.get_left() == expected_left[0][marker_index]);
    REQUIRE(fixture.m_display.get_relative_cursor().x == expected_cursor[0][marker_index]);

    text.append("x");
    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) ==
            (tib::c_horz_scroll_indicator_chars == 1));
    REQUIRE(fixture.m_display.get_left() == expected_left[1][marker_index]);
    REQUIRE(fixture.m_display.get_relative_cursor().x == expected_cursor[1][marker_index]);

    text.append("x");
    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) ==
            (tib::c_horz_scroll_indicator_chars == 1));
    REQUIRE(fixture.m_display.get_left() == expected_left[2][marker_index]);
    REQUIRE(fixture.m_display.get_relative_cursor().x == expected_cursor[2][marker_index]);
}

TEST_CASE("Display horizontal scroll marker placement")
{
    display_test_fixture fixture(10, true);
    fixture.m_buffer.set_text("abcdefghijk", 0);

    REQUIRE(fixture.m_display.display() == true);
    tib::cstring expected;
    expected.set("abcdefghijk", 10 - tib::c_horz_scroll_indicator_chars);
    expected.append("\x1b[1m");
    expected.append_char('>', tib::c_horz_scroll_indicator_chars);
    REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
}

TEST_CASE("Display horizontal scrolling keeps caret out of right marker")
{
    constexpr uint16_t max_width = 10;
    constexpr uint16_t left_text_width = 2;
    display_test_fixture fixture(max_width, true);
    fixture.m_display.set_left_text("> ", left_text_width);
    const tib::textpos_t caret = max_width - tib::c_horz_scroll_indicator_chars - left_text_width;
    fixture.m_buffer.set_text("abcdefghijk", caret);

    REQUIRE(fixture.m_display.display() == true);
    REQUIRE(fixture.m_display.get_left() == 1);
    REQUIRE(fixture.m_display.get_relative_cursor().x <
            max_width - tib::c_horz_scroll_indicator_chars);
}

TEST_CASE("Display horizontal scrolling keeps caret out of left marker")
{
    display_test_fixture fixture(10, true);
    fixture.m_display.set_left_text("> ", 2);
    fixture.m_display.set_scroll_offsets(2, 0);
    REQUIRE(fixture.display_initial("abcdefghijk", 6) == true);
    REQUIRE(fixture.m_display.get_left() == 2);

    fixture.m_buffer.set_selection(2, 2);
    REQUIRE(fixture.m_display.display() == true);
    REQUIRE(fixture.m_display.get_left() == 0);
    const tib::coord expected = { 4, 0 };
    REQUIRE(fixture.m_display.get_relative_cursor() == expected);
}

TEST_CASE("Display multiline wrapping")
{
    display_test_fixture fixture(40, false, 2);
    tib::cstring text;
    for (uint32_t i = 0; i < 39; ++i)
        text.append("x");
    text.append("\xe2\x9c\x94\xef\xb8\x8f"); // U+2714 U+FE0F.

    REQUIRE(fixture.display_initial(text.c_str(), 39) == true);
    REQUIRE(fixture.m_display.get_relative_cursor().x == 0);
    REQUIRE(fixture.m_display.get_relative_cursor().y == 1);

    REQUIRE(fixture.display_initial(text.c_str(), 42) == false);
    REQUIRE(fixture.m_display.get_relative_cursor().x == 0);
    REQUIRE(fixture.m_display.get_relative_cursor().y == 1);

    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) == false);
    REQUIRE(fixture.m_display.get_relative_cursor().x == 2);
    REQUIRE(fixture.m_display.get_relative_cursor().y == 1);
}

TEST_CASE("Display full-width pending wrap")
{
    const char* const pending_wrap_suffix = tib::is_autowrap_bug_present() ? "\r" : "\x1b[m \x08";

    SECTION("Finishes pending wrap after a final text cell")
    {
        display_test_fixture fixture(tib::int16_max, false, 3, true);
        tib::cstring text;
        text.append_char('x', tib::get_terminal_size().x);
        fixture.m_buffer.set_text(text.c_str(), tib::textpos_t(text.length()));

        REQUIRE(fixture.m_display.display() == true);
        tib::cstring expected_output(text);
        expected_output.append(pending_wrap_suffix);
        REQUIRE(strstr(s_display_output.c_str(), expected_output.c_str()) != nullptr);
        const tib::coord expected = { 0, 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }

    SECTION("Finishes pending wrap after a final space cell")
    {
        display_test_fixture fixture(tib::int16_max, false, 3, true);
        tib::cstring text;
        text.append_char('x', tib::get_terminal_size().x - 1);
        text.append(" ");
        fixture.m_buffer.set_text(text.c_str(), tib::textpos_t(text.length()));

        REQUIRE(fixture.m_display.display() == true);
        tib::cstring expected_output(text);
        expected_output.append(pending_wrap_suffix);
        REQUIRE(strstr(s_display_output.c_str(), expected_output.c_str()) != nullptr);
        const tib::coord expected = { 0, 1 };
        REQUIRE(fixture.m_display.get_relative_cursor() == expected);
    }
}

TEST_CASE("Display border pending wrap")
{
    tib::border_definition border;
    border.top = "-";
    border.bottom = "=";
    border.left = "||";
    border.right = "||";
    border.right_2 = "!!";
    border.top_width = 1;
    border.bottom_width = 1;
    border.left_width = 2;
    border.right_width = 2;
    const char* const suffix = tib::is_autowrap_bug_present() ? "\r" : "\x1b[m \x08";

    SECTION("Finishes wraps at the terminal edge with an offset origin")
    {
        display_test_fixture fixture(tib::int16_max, false, 2, false, &border, 5);
        fixture.m_buffer.set_text("abc", 3);
        REQUIRE(fixture.m_display.display() == true);
        tib::cstring expected("||");
        expected.append(suffix);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
        expected.set("!!");
        expected.append(suffix);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
        expected.set("-");
        expected.append(suffix);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
        expected.set("=");
        expected.append(suffix);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
        const char* const border_end = strstr(s_display_output.c_str(), "\x1b[4A");
        REQUIRE(border_end != nullptr);
        const char* const newline = strstr(s_display_output.c_str(), "\r\n");
        REQUIRE((!newline || newline > border_end));
    }

    SECTION("Leaves borders short of the terminal edge unchanged")
    {
        display_test_fixture fixture(10, false, 2, false, &border, 5);
        fixture.m_buffer.set_text("abc", 3);
        REQUIRE(fixture.m_display.display() == true);
        REQUIRE(strstr(s_display_output.c_str(), "||\r\n") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "!!\r\n") != nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[m \x08") == nullptr);
        REQUIRE(strstr(s_display_output.c_str(), "\x1b[3A") != nullptr);
    }
}

TEST_CASE("Display multiline scroll markers")
{
    SECTION("Preserves line width when replacing trailing text")
    {
        tib::display_lines lines;
        lines.m_lines.emplace_back(std::make_unique<tib::display_line>(1));
        auto line = std::make_unique<tib::display_line>(1);
        line->append("abc ", 4, 4, tib::FACE_DEFAULT);
        lines.m_lines.emplace_back(std::move(line));

        lines.apply_scroll_markers(4, 2, 3);
        tib::cstring expected;
        expected.set("abc ", 4 - tib::c_horz_scroll_indicator_chars);
        expected.append_char('>', tib::c_horz_scroll_indicator_chars);
        REQUIRE(lines.m_lines.back()->m_text.equals(expected.c_str()));
        REQUIRE(lines.m_lines.back()->width() == 4);
    }

    SECTION("Pads only the bottom row that needs a marker")
    {
        tib::display_lines lines;
        auto first = std::make_unique<tib::display_line>(1);
        first->append("x", 1, 1, tib::FACE_DEFAULT);
        lines.m_lines.emplace_back(std::move(first));
        auto bottom = std::make_unique<tib::display_line>(1);
        bottom->append("abc ", 4, 4, tib::FACE_DEFAULT);
        lines.m_lines.emplace_back(std::move(bottom));

        lines.apply_scroll_markers(10, 2, 3);
        REQUIRE(lines.m_lines.front()->width() == 1);
        REQUIRE(lines.m_lines.back()->width() == 10);
    }

    SECTION("Pads newline-terminated rows before applying the marker")
    {
        display_test_fixture fixture(10, false, 2);
        fixture.m_buffer.set_text("x\nabc \ndef", 2);

        REQUIRE(fixture.m_display.display() == true);
        tib::cstring expected;
        expected.set("abc");
        expected.append_char(' ', 7 - tib::c_horz_scroll_indicator_chars);
        expected.append("\x1b[1m");
        expected.append_char('>', tib::c_horz_scroll_indicator_chars);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
    }

    SECTION("Applies a marker to a blank newline-delimited row")
    {
        display_test_fixture fixture(10, false, 2);
        fixture.m_buffer.set_text("x\n\nz", 4);

        REQUIRE(fixture.m_display.display() == true);
        tib::cstring expected;
        expected.set("\x1b[1m");
        expected.append_char('<', tib::c_horz_scroll_indicator_chars);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
    }

    SECTION("Pads rows when the next grapheme does not fit")
    {
        display_test_fixture fixture(10, false, 2);
        fixture.m_buffer.set_text("x\n123456789\xe4\xb8\xadz", 2);

        REQUIRE(fixture.m_display.display() == true);
        tib::cstring expected;
        expected.set("123456789 ", 10 - tib::c_horz_scroll_indicator_chars);
        expected.append("\x1b[1m");
        expected.append_char('>', tib::c_horz_scroll_indicator_chars);
        REQUIRE(strstr(s_display_output.c_str(), expected.c_str()) != nullptr);
    }
}

TEST_CASE("Display variable height scrolling")
{
    display_test_fixture fixture(10, false, 3, true);
    tib::cstring text;
    text.append("xxxxxxxxxx", 10);
    text.append("xxxxxxxxxx", 10);
    text.append("xxxxxxxxxx", 10);
    text.append("xxxxxxxxxx", 10);

    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) == true);
    REQUIRE(fixture.m_display.get_top() == 2);

    text.set_length(31);
    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) == true);
    REQUIRE(fixture.m_display.get_top() == 1);

    text.set_length(15);
    REQUIRE(fixture.display_initial(text.c_str(), tib::textpos_t(text.length())) == true);
    REQUIRE(fixture.m_display.get_top() == 0);
}

static void make_matching_display_line_data(tib::cstring& text, tib::cstring& matching_text,
                                            tib::cstring& faces, tib::cstring& matching_faces)
{
    text.set("The quick brown fox jumps over the lazy dog. "
             "The quick brown fox jumps over the lazy dog.");
    matching_text = text;
    faces.append_spaces(text.length());
    matching_faces = faces;
}

PERF_CASE("PERF, compare matching display line with memcmp")
{
    tib::cstring text;
    tib::cstring matching_text;
    tib::cstring faces;
    tib::cstring matching_faces;
    make_matching_display_line_data(text, matching_text, faces, matching_faces);

    uint32_t matches = 0;
    for (uint32_t pass = 0; pass < c_display_line_comparison_passes; ++pass)
    {
        if (text == matching_text && faces == matching_faces)
            ++matches;
    }

    REQUIRE(matches == c_display_line_comparison_passes);
}

PERF_CASE("PERF, compare matching display line by grapheme")
{
    tib::cstring text;
    tib::cstring matching_text;
    tib::cstring faces;
    tib::cstring matching_faces;
    make_matching_display_line_data(text, matching_text, faces, matching_faces);

    uint32_t matches = 0;
    for (uint32_t pass = 0; pass < c_display_line_comparison_passes; ++pass)
    {
        size_t pos = 0;
        size_t matching_pos = 0;
        while (pos < text.length() && matching_pos < matching_text.length())
        {
            const size_t next = tib::forward_one_grapheme(text.c_str(), text.length(), uint32_t(pos));
            const size_t matching_next = tib::forward_one_grapheme(matching_text.c_str(), matching_text.length(), uint32_t(matching_pos));
            const size_t length = next - pos;
            const size_t matching_length = matching_next - matching_pos;
            if (length != matching_length ||
                memcmp(text.c_str() + pos, matching_text.c_str() + matching_pos, length) != 0 ||
                memcmp(faces.c_str() + pos, matching_faces.c_str() + matching_pos, length) != 0)
                break;

            pos = next;
            matching_pos = matching_next;
        }

        if (pos == text.length() && matching_pos == matching_text.length())
            ++matches;
    }

    REQUIRE(matches == c_display_line_comparison_passes);
}
