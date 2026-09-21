// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#include "pch.h"
#include "maybe_windows.h"
#include "tib.h"
#include "wcwidth.h"
#include <cctype>
#include <cwctype>
#include <assert.h>

namespace tib {

bool g_optimize_self_insert = true;

constexpr int32_t c_max_numeric_argument = 1000000; // REVIEW: add to editor_quirks?

constexpr uint8_t NUMFLAG_AUTO_CLEAR        = 0x01; // Clears numeric argument at end of dispatch.
constexpr uint8_t NUMFLAG_HAS_ARGUMENT      = 0x02; // An explicit numeric argument is present.
constexpr uint8_t NUMFLAG_ARGUMENT_MODE     = 0x04; // Digit-argument mode; behaves slightly differently than universal-argument mode.
constexpr uint8_t NUMFLAG_HAS_ARG_DIGITS    = 0x08; // Because minus sign is not a digit.
constexpr uint8_t NUMFLAG_UNIVERSAL_MODE    = 0x10; // Universal-argument mode; behaves slightly differently than digit-argument mode.
constexpr uint8_t NUMFLAG_HAS_UNI_DIGITS    = 0x20; // Digits entered since the last universal-argument.

static std::shared_ptr<const key_table_list> make_digit_argument_key_table(bool leading_minus=false)
{
    auto table = std::make_shared<key_table>(false/*can_self_insert*/);
    char seq[3] = { 0x1b, '0' };
    while (seq[1] <= '9')
    {
        table->add(seq, binding_target_func("digit-argument"));
        table->add(seq + 1, binding_target_func("digit-argument"));
        ++seq[1];
    }
    if (leading_minus)
    {
        seq[1] = '-';
        table->add(seq, binding_target_func("digit-argument"));
        table->add(seq + 1, binding_target_func("digit-argument"));
    }

    auto table_list = std::make_shared<key_table_list>();
    table_list->emplace_back(std::move(table));
    return table_list;
}

static std::shared_ptr<const key_table_list> get_digit_argument_key_table()
{
    static const auto s_digits_table_list = make_digit_argument_key_table();
    return s_digits_table_list;
}

static std::shared_ptr<const key_table_list> get_universal_argument_key_table()
{
    static const auto s_universal_table_list = make_digit_argument_key_table(true/*leading_minus*/);
    return s_universal_table_list;
}

undo_entry::~undo_entry()
{
    assert(!m_prev);
    assert(!m_next);
}

void undo_entry::link_at_tail(undo_entry*& head, undo_entry*& tail)
{
    assert(!m_prev);
    assert(!m_next);
    m_prev = tail;
    if (tail)
        tail->m_next = this;
    if (!head)
        head = this;
    tail = this;
}

void undo_entry::unlink(undo_entry*& head, undo_entry*& tail)
{
    if (m_prev)
        m_prev->m_next = m_next;
    else
        head = m_next;
    if (m_next)
        m_next->m_prev = m_prev;
    else
        tail = m_prev;
    m_prev = nullptr;
    m_next = nullptr;
}

static bool is_word_char(const char* s, textpos_t len, textpos_t pos, uint8_t word)
{
    assert(pos >= 0);
    assert(pos <= len);
    assert(word);
    str_iter iter(s + pos, len - pos);

    const char32_t c = iter.next();
    if (word > 1)
        return c > 0x7f || !std::isspace(uint8_t(c));

    if (c >= 0xd800)
        return true;

    return std::iswalnum(uint16_t(c));
}

textpos_t pos_mover(const char* s, const size_t _len, textpos_t& pos, const bool forward, const uint8_t word)
{
    assert(pos >= 0);
    const textpos_t len = textpos_t(_len);
    assert(len >= 0);
    assert(pos <= len);

    // Try to make sure pos is at a valid codepoint boundary.
    if (pos > 0 && pos < len)
    {
        const textpos_t prev = backward_one_grapheme(s, _len, pos);
        const textpos_t next = forward_one_grapheme(s, _len, prev);
        if (forward)
        {
            if (next > pos)
                pos = prev;
        }
        else
        {
            if (next > pos)
                pos = next;
        }
    }

    const textpos_t orig_pos = pos;

    if (forward)
    {
        if (pos < len)
        {
            if (!word)
            {
                pos = forward_one_grapheme(s, _len, pos);
            }
            else
            {
                while (pos < len)
                {
                    const textpos_t test_pos = forward_one_grapheme(s, _len, pos);
                    if (is_word_char(s, len, pos, word))
                        break;
                    pos = test_pos;
                }
                while (pos < len)
                {
                    const textpos_t test_pos = forward_one_grapheme(s, _len, pos);
                    if (!is_word_char(s, len, pos, word))
                        break;
                    pos = test_pos;
                }
            }

            if (pos > len)
                pos = len;
        }
    }
    else
    {
        if (pos > 0)
        {
            if (!word)
            {
                pos = backward_one_grapheme(s, _len, pos);
            }
            else
            {
                while (pos > 0)
                {
                    const textpos_t test_pos = backward_one_grapheme(s, _len, pos);
                    if (is_word_char(s, len, test_pos, word))
                        break;
                    pos = test_pos;
                }
                while (pos > 0)
                {
                    const textpos_t test_pos = backward_one_grapheme(s, _len, pos);
                    if (!is_word_char(s, len, test_pos, word))
                        break;
                    pos = test_pos;
                }
            }

            if (pos >= len)
                pos = 0;
        }
    }

    const textpos_t begin = min(pos, orig_pos);
    const textpos_t end = max(pos, orig_pos);
    const textpos_t moved = end - begin;
    return moved;
}

editor_context::~editor_context()
{
    clear_undo_internal();
}

editor_context::editor_context()
{
    ensure_commands();

    init_undo();
    m_display.init_buffer(this);
    m_display.init_layout(&m_layout);
    m_display.init_style(&m_style);
}

void editor_context::set_overwrite_mode(bool overwrite) noexcept
{
    clear_overwrite_input();
    m_overwrite_mode = overwrite;
}

void editor_context::set_border(const border_definition* border)
{
    if (border && !border->has_top() && !border->has_bottom() && !border->has_left() && !border->has_right())
        border = nullptr;
    if (m_style.border != border)
        m_display.invalidate_border();
    m_style.border = border;
}

#if 0
void editor_context::set_history(std::vector<cstring>* history)
{
    m_history = history;
    m_history_index = m_history ? m_history->size() : 0;
}
#endif

void editor_context::initialize(const char* text, size_t len)
{
    m_done = false;
    m_overwrite_mode = false;

    reset_state();

    if (!text)
    {
        text = "";
        len = 0;
    }
    else
    {
        len = min<size_t>(int16_max, resolve_auto_length(len, text));
    }

    clear_undo_internal();
    m_selection.set_caret(0);
    m_selection.set_mark(0);
    m_selection.set_mark_active(false);
    m_auto_deactivate_mark = true;
    insert_text(text, len);
    m_selection.clear_dirty();
    m_display.clear_scroll_offsets();
    init_undo();

    m_quoted_insert_count = 0;

    assert(!m_selection.is_dirty());
    assert(m_selection.get_caret() == len);
    assert(m_selection.get_anchor() == len);
    assert(m_text.length() == len);
    assert(!m_defer_init_undo);

#if 0
    m_history_index = m_history ? m_history->size() : 0;
#endif
}

void editor_context::reset_state() noexcept
{
    m_can_drag = false;
    clear_overwrite_input();
    m_last_command.clear();
    m_named_values.clear();
    clear_numeric_argument();
}

void editor_context::set_callbacks(editor_callbacks* callbacks)
{
    m_callbacks = callbacks;
    m_display.init_callbacks(m_callbacks);
}

std::shared_ptr<const color_table> editor_context::get_color_table() const
{
    return m_display.get_color_table();
}

void editor_context::set_color_table(std::shared_ptr<const color_table> colors)
{
    m_display.set_color_table(colors);
}

void editor_context::set_face_defs(const face_definitions* face_defs)
{
    m_display.init_faces(face_defs);
}

void editor_context::set_empty_face(char face)
{
    m_style.empty_face = face;
    m_display.force_redisplay();
}

void editor_context::set_left_text(const char* left, uint16_t width)
{
    m_display.set_left_text(left, width);
}

void editor_context::set_right_text(const char* right, uint16_t width)
{
    m_display.set_right_text(right, width);
}

void editor_context::set_suggestion_text(const char* suggestion, size_t len)
{
    len = resolve_auto_length(len, suggestion);
    m_display.set_suggestion_text(suggestion, len);
}

void editor_context::set_usage_text(const char* usage, uint16_t width)
{
    m_display.set_usage_text(usage, width);
}

void editor_context::set_additional_lines(const std::vector<additional_display_line>& lines)
{
    m_display.set_additional_lines(lines);
}

void editor_context::clear_additional_lines()
{
    m_display.clear_additional_lines();
}

#if 0
int32_t editor_context::go(void* cookie)
{
    const HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hout, &csbi);

    if (unsigned(csbi.dwCursorPosition.X) + 8 >= unsigned(csbi.dwSize.X))
        return false;
    if (unsigned(csbi.dwCursorPosition.X) + m_layout.max_width >= unsigned(csbi.dwSize.X))
        m_layout.max_width = csbi.dwSize.X - csbi.dwCursorPosition.X;

    if (m_layout.origin.X < 0 || m_layout.origin.Y < 0)
        SetOrigin(csbi.dwCursorPosition);

    AutoMouseConsoleMode mouse(g_options.allow_mouse);
    m_mouse_helper.ClearClicks();
    m_can_drag = false;

#ifdef DEBUG
    StrW prev_text(m_s);
    uint32 prev_counter = m_change_counter;
#endif

    while (true)
    {
        ensure_left();
        print_visible();

#ifdef DEBUG
        // Verify any time m_s changes then m_change_counter also increases.
        if (!prev_text.Equal(m_s))
        {
            assert(int32(m_change_counter) - int32(prev_counter) > 0);
            prev_text.Set(m_s);
            prev_counter = m_change_counter;
        }
#endif

        const InputRecord input = SelectInput(INFINITE, &mouse);
        switch (input.type)
        {
        case InputType::None:
        case InputType::Error:
            continue;
        case InputType::Resize:
            return false;

        case InputType::Key:
        case InputType::Char:
        case InputType::Mouse:
            if (m_callback)
            {
                const int32 result = (*m_callback)(input, *this, cookie);
                // Negative means break out of the loop.
                if (result < 0)
                    return -1;
                // Positive means do not process (already handled).
                if (result > 0)
                    continue;
                // Zero means allow normal processing.
            }
            switch (HandleInput(input))
            {
            case Outcome::Cancelled:
                return false;
            case Outcome::Done:
                return true;
            case Outcome::DontResetHistoryIndex:
                break;
            case Outcome::ResetHistoryIndex:
                if (m_history && m_history_index < m_history->size())
                    m_history_index = m_history->size();
                break;
            default:
                assert(false);
                break;
            }
            break;

        default:
            assert(false);
            break;
        }
    }
}
#endif

void editor_context::begin_display()
{
    m_display.begin_display();
}

void editor_context::invalidate()
{
    m_display.invalidate();
}

void editor_context::invalidate_border()
{
    m_display.invalidate_border();
}

void editor_context::display()
{
    m_display.display();
}

void editor_context::force_redisplay()
{
    m_display.force_redisplay();
}

void editor_context::move_to_end_of_display()
{
    m_display.move_to_end_of_display();
}

void editor_context::move_to_caret_position()
{
    m_display.move_to_caret_position();
}

void editor_context::erase_display()
{
    m_display.erase_display();
}

void editor_context::end_display_lf()
{
    m_display.end_display_lf();
}

void editor_context::begin_of_input(bool select)
{
    if (!select)
        m_selection.set_caret(0);
    else if (!m_selection.has_selection())
        m_selection.set_selection(m_selection.get_caret(), 0);
    else
        m_selection.set_selection(m_selection.get_anchor(), 0);

    m_display.clear_scroll_offsets();

    if (!select)
        m_selection.reset_word_anchor();
}

void editor_context::end_of_input(bool select)
{
    if (!select)
        m_selection.set_caret(textpos_t(m_text.length()));
    else if (!m_selection.has_selection())
        m_selection.set_selection(m_selection.get_caret(), textpos_t(m_text.length()));
    else
        m_selection.set_selection(m_selection.get_anchor(), textpos_t(m_text.length()));

    if (!select)
        m_selection.reset_word_anchor();
}

bool editor_context::move_left(uint8_t word, bool select)
{
    bool moved = false;
    if (!select && m_selection.has_selection())
    {
        m_selection.set_caret(m_selection.get_sel_begin());
        moved = true;
    }
    else if (m_selection.get_caret() > 0)
    {
        textpos_t caret = m_selection.get_caret();
        textpos_t anchor = m_selection.get_anchor();
        pos_mover(m_text.c_str(), m_text.length(), caret, false/*forward*/, word);
        moved = m_selection.set_selection(select ? anchor : caret, caret);
    }
    if (!select)
        m_selection.reset_word_anchor();
    return moved;
}

bool editor_context::move_right(uint8_t word, bool select)
{
    bool moved = false;
    if (!select && m_selection.has_selection())
    {
        m_selection.set_caret(m_selection.get_sel_end());
        moved = true;
    }
    else if (size_t(m_selection.get_caret()) < m_text.length())
    {
        textpos_t caret = m_selection.get_caret();
        textpos_t anchor = m_selection.get_anchor();
        pos_mover(m_text.c_str(), m_text.length(), caret, true/*forward*/, word);
        moved = m_selection.set_selection(select ? anchor : caret, caret);
    }
    if (!select)
        m_selection.reset_word_anchor();
    return moved;
}

bool editor_context::backspace(uint8_t word)
{
    m_selection.reset_word_anchor();
    if (!m_selection.has_selection() && m_selection.get_caret() <= 0)
        return false;

    begin_undo_group();

    if (!elide_selected_text())
    {
#ifdef DEBUG
        const textpos_t old_pos = m_selection.get_caret();
#endif
        textpos_t caret = m_selection.get_caret();
        const textpos_t moved = pos_mover(m_text.c_str(), m_text.length(), caret, false/*forward*/, word);
        m_selection.set_caret(caret);
#ifdef DEBUG
        assert(old_pos == m_selection.get_caret() + moved);
#endif
        remove_text(m_selection.get_caret(), m_selection.get_caret() + moved);
    }

    end_undo_group();
    return true;
}

bool editor_context::del(uint8_t word)
{
    m_selection.reset_word_anchor();
    if (!m_selection.has_selection() && m_selection.get_caret() >= textpos_t(m_text.length()))
        return false;

    begin_undo_group();

    if (!elide_selected_text())
    {
        textpos_t del_pos = m_selection.get_caret();
        const textpos_t moved = pos_mover(m_text.c_str(), m_text.length(), del_pos, true/*forward*/, word);
        m_selection.set_caret(del_pos - moved);
        remove_text(m_selection.get_caret(), m_selection.get_caret() + moved);
    }

    end_undo_group();
    return true;
}

void editor_context::del_line()
{
    remove_text(0, -1);

    assert(!get_caret());
    assert(!get_mark());
    assert(!get_text().length());
    assert(!get_selection_state().get_anchor());
}

void editor_context::clear_selection()
{
    m_selection.clear_selection();
}

bool editor_context::set_caret(textpos_t caret)
{
    return m_selection.set_caret(caret);
}

bool editor_context::set_selection(textpos_t anchor, textpos_t caret)
{
    if (!m_selection.set_selection(anchor, caret))
        return false;
    m_selection.reset_word_anchor();
    return true;
}

bool editor_context::extend_selection(textpos_t pos, uint8_t word)
{
    textpos_t begin;
    textpos_t end;
    get_range_at_click(pos, word, begin, end);

    textpos_t apply_begin;
    textpos_t apply_end;
    if (begin < m_selection.get_word_anchor_begin())
    {
        apply_begin = m_selection.get_word_anchor_end();
        apply_end = begin;
    }
    else if (end > m_selection.get_word_anchor_end())
    {
        apply_begin = m_selection.get_word_anchor_begin();
        apply_end = end;
    }
    else
    {
        apply_begin = m_selection.get_word_anchor_begin();
        apply_end = m_selection.get_word_anchor_end();
    }

    return m_selection.set_selection(apply_begin, apply_end);
}

void editor_context::get_range_at_click(textpos_t pos, uint8_t word, textpos_t& begin, textpos_t& end)
{
    const textpos_t orig_pos = pos;

    // Look forward (for a word).
    pos_mover(m_text.c_str(), m_text.length(), pos, true/*forward*/, word);
    end = pos;
    pos_mover(m_text.c_str(), m_text.length(), pos, false/*forward*/, word);
    const textpos_t high_mid = pos;

    // Look backward (for a word).
    pos_mover(m_text.c_str(), m_text.length(), pos, false/*forward*/, word);
    begin = pos;
    pos_mover(m_text.c_str(), m_text.length(), pos, true/*forward*/, word);
    const textpos_t low_mid = pos;

    if (high_mid <= orig_pos)
    {
        begin = high_mid;
    }
    else if (low_mid > orig_pos)
    {
        end = low_mid;
    }
    else
    {
        // The position is in between (two words); select the text between.
        begin = low_mid;
        end = high_mid;
    }
}

bool editor_context::select_word(bool bigword)
{
    textpos_t begin;
    textpos_t end;

    const uint8_t word = bigword ? 2 : 1;
    get_range_at_click(m_selection.get_caret(), word, begin, end);

    return set_selection(begin, end);
}

bool editor_context::set_mark(textpos_t mark)
{
    if (mark < 0 || size_t(mark) > get_text().length())
        return false;
    if (!m_selection.set_mark(mark))
        return false;
    if (m_selection.is_mark_active())
        m_display.invalidate();
    return true;
}

bool editor_context::set_mark_active(bool active)
{
    if (!m_selection.set_mark_active(active))
        return false;
    m_display.invalidate();
    return true;
}

void editor_context::clear_auto_deactivate_mark()
{
    m_auto_deactivate_mark = false;
}

bool editor_context::exchange_caret_and_mark()
{
    const textpos_t mark = get_mark();
    if (mark < 0 || size_t(mark) > get_text().length())
    {
        set_mark(0);
        return false;
    }

    set_mark(get_caret());
    set_caret(mark);

    set_mark_active();
    clear_auto_deactivate_mark();
    return true;
}

bool editor_context::transpose(uint8_t word)
{
    const textpos_t anchor = m_selection.get_anchor();
    const textpos_t caret = m_selection.get_caret();

    clear_selection();
    move_right(word);
    move_left(word);
    const textpos_t second_begin = get_caret();
    move_left(word);
    const textpos_t first_begin = get_caret();
    move_right(word);
    const textpos_t first_end = get_caret();
    move_right(word);
    const textpos_t second_end = get_caret();

    if (first_begin >= first_end || first_end > second_begin || second_begin >= second_end)
    {
        set_selection(anchor, caret);
        return false;
    }

    cstring transposed;
    const char* const t = m_text.c_str();
    transposed.append(t + second_begin, second_end - second_begin);
    transposed.append(t + first_end, second_begin - first_end);
    transposed.append(t + first_begin, first_end - first_begin);

    set_selection(anchor, caret);
    begin_undo_group();
    clear_selection();
    set_caret(first_begin);
    remove_text(first_begin, second_end);
    assert(get_caret() == first_begin);
    insert_text(transposed.c_str(), transposed.length());
    set_caret(second_end);
    end_undo_group();
    return true;
}

#ifdef _WIN32
bool editor_context::copy_to_clipboard()
{
    if (!m_selection.has_selection())
        return false;

    const textpos_t begin = m_selection.get_sel_begin();
    const textpos_t end = m_selection.get_sel_end();
    const textpos_t len = (end - begin);

    cstring_t<WCHAR> tmp;
    if (!to_utf16(m_text.c_str() + begin, len, tmp))
        return false;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT, (len + 1) * sizeof(WCHAR));
    if (mem == nullptr)
        return false;

    WCHAR* data = (WCHAR*)GlobalLock(mem);
    memcpy(data, tmp.c_str(), tmp.length() * sizeof(WCHAR));
    data[tmp.length()] = 0;
    GlobalUnlock(mem);

    if (!OpenClipboard(0))
    {
        GlobalFree(mem);
        return false;
    }

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

bool editor_context::cut_to_clipboard()
{
    begin_undo_group();
    const bool copied = copy_to_clipboard();
    elide_selected_text();
    end_undo_group();
    return copied;
}

bool editor_context::paste_from_clipboard()
{
    if (!OpenClipboard(0))
        return false;

    bool pasted = false;
    HANDLE mem = GetClipboardData(CF_UNICODETEXT);
    if (mem)
    {
        size_t len = size_t(GlobalSize(mem) / sizeof(WCHAR));
        LPCWSTR data = LPCWSTR(GlobalLock(mem));

        while (len && !data[len - 1])
            --len;

        cstring tmp;
        if (to_utf8(data, len, tmp))
        {
            pasted = !tmp.empty();
            insert_text(tmp.c_str(), tmp.length());
        }

        GlobalUnlock(mem);
    }

    CloseClipboard();
    return pasted;
}
#endif

#if 0
void editor_context::replace_from_history(const cstring& s, bool keep_undo)
{
    inc_change_counter();

    m_text.set(s);
    m_selection.set_caret(m_text.Length());
    m_defer_init_undo = !keep_undo;

    coord max_size = get_effective_max_size();
    max_size.x = max<int16_t>(2, max_size.x);
    m_left = back_up_by_amount(get_caret(), m_text.c_str(), m_left, max_size.x - 1);
}
#endif

void editor_context::set_last_command(const char* name)
{
    m_last_command.set(name);
}

const char* editor_context::get_named_value(const char* name) const
{
    const auto found = m_named_values.find(name);
    return (found == m_named_values.end()) ? nullptr : found->second.c_str();
}

int32_t editor_context::get_named_value_int(const char* name, int32_t def) const
{
    const char* value = get_named_value(name);
    if (!value)
        return def;
    return atoi(value);
}

void editor_context::set_named_value(const char* name, const char* value)
{
    auto found = m_named_values.find(name);
    if (found == m_named_values.end())
        m_named_values.emplace(cstring(name), cstring(value));
    else
        found->second.set(value);
}

void editor_context::set_named_value_int(const char* name, int32_t value)
{
    cstring tmp;
    tmp.printf("%d", value);

    auto found = m_named_values.find(name);
    if (found == m_named_values.end())
        m_named_values.emplace(cstring(name), std::move(tmp));
    else
        found->second = std::move(tmp);
}

void editor_context::clear_named_value(const char* name)
{
    m_named_values.erase(name);
}

void editor_context::set_auto_clear_numeric_argument(bool clear)
{
    if (clear)
        m_numflags |= NUMFLAG_AUTO_CLEAR;
    else
        m_numflags &= ~NUMFLAG_AUTO_CLEAR;
}

void editor_context::clear_numeric_argument()
{
    m_numflags = 0;
    m_sign_numeric_argument = 1;
    m_numeric_argument = 0;
    apply_override_bindings();
    apply_message_text();
}

bool editor_context::has_numeric_argument() const
{
    return !!(m_numflags & NUMFLAG_HAS_ARGUMENT);
}

int32_t editor_context::get_argument_sign() const
{
    return m_sign_numeric_argument;
}

void editor_context::set_argument_sign(int32_t sign)
{
    m_sign_numeric_argument = (sign >= 0) ? 1 : -1;
    apply_message_text();
}

void editor_context::invert_argument_sign()
{
    m_sign_numeric_argument = 0 - m_sign_numeric_argument;
    apply_message_text();
}

int32_t editor_context::get_numeric_argument() const
{
    return has_numeric_argument() ? m_numeric_argument * m_sign_numeric_argument : 1;
}

void editor_context::set_numeric_argument(int32_t value)
{
    m_numflags |= NUMFLAG_HAS_ARGUMENT|NUMFLAG_HAS_ARG_DIGITS;
    m_sign_numeric_argument = (value >= 0) ? 1 : -1;
    m_numeric_argument = (value >= 0) ? value : 0 - value;
    apply_message_text();
}

bool editor_context::numeric_digit(int32_t key)
{
    switch (key)
    {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        if (has_numeric_argument())
        {
            if (!m_quirks.bash_digit_argument && !(m_numflags & NUMFLAG_HAS_ARG_DIGITS))
                m_numeric_argument = 0;
            m_numeric_argument *= 10;
            m_numeric_argument += (key - '0');
            if (m_numeric_argument > c_max_numeric_argument)
            {
                clear_numeric_argument();
                return false;
            }
        }
        else
        {
            clear_numeric_argument();
            m_numflags |= NUMFLAG_HAS_ARGUMENT;
            m_numeric_argument = (key - '0');
        }
        m_numflags |= NUMFLAG_HAS_ARG_DIGITS;
        if (m_numflags & NUMFLAG_UNIVERSAL_MODE)
            m_numflags |= NUMFLAG_HAS_UNI_DIGITS;
        break;
    case '-':
        if ((m_numflags & NUMFLAG_UNIVERSAL_MODE) &&
            !(m_numflags & NUMFLAG_HAS_ARG_DIGITS) &&
            m_sign_numeric_argument > 0)
        {
            m_sign_numeric_argument = -1;
            m_numeric_argument = 1;
        }
        else if (has_numeric_argument())
        {
            int32_t repeat = get_numeric_argument();
            clear_numeric_argument();
            if (repeat > 0)
            {
                begin_undo_group();
                while (repeat-- > 0)
                    insert_text("-", 1, get_overwrite_mode());
                end_undo_group();
            }
            return true;
        }
        else
        {
            clear_numeric_argument();
            m_numflags |= NUMFLAG_HAS_ARGUMENT;
            m_sign_numeric_argument = -1;
            m_numeric_argument = 1;
        }
        break;
    default:
        return false;
    }

    set_auto_clear_numeric_argument(false);
    m_numflags |= NUMFLAG_ARGUMENT_MODE;
    apply_override_bindings();
    apply_message_text();
    return true;
}

bool editor_context::universal_argument()
{
    const bool uni_mode = !!(m_numflags & NUMFLAG_UNIVERSAL_MODE);
    const bool uni_digits = !!(m_numflags & NUMFLAG_HAS_UNI_DIGITS);
    assert(implies(!uni_mode, !uni_digits));

    if (uni_mode && uni_digits)
    {
        m_numflags &= ~(NUMFLAG_ARGUMENT_MODE|NUMFLAG_HAS_UNI_DIGITS);
    }
    else
    {
        if (uni_mode || !has_numeric_argument())
        {
            if (!uni_mode)
            {
                clear_numeric_argument();
                m_numflags |= NUMFLAG_HAS_ARGUMENT;
                m_sign_numeric_argument = 1;
                m_numeric_argument = 1;
            }

            m_numeric_argument *= 4;
            if (m_numeric_argument > c_max_numeric_argument)
            {
                clear_numeric_argument();
                return false;
            }

            m_numflags |= NUMFLAG_ARGUMENT_MODE;
            m_numflags &= ~NUMFLAG_HAS_ARG_DIGITS;
        }

        m_numflags |= NUMFLAG_UNIVERSAL_MODE;
    }

    set_auto_clear_numeric_argument(false);
    apply_override_bindings();
    apply_message_text();
    return true;
}

bool editor_context::scroll_horizontally(int32_t columns, int32_t cursor_column, bool exclude_auto_scroll)
{
    return m_display.scroll_horizontally(columns, cursor_column, m_selection, exclude_auto_scroll);
}

bool editor_context::move_caret_vertically(int32_t rows, int32_t cursor_column, bool select)
{
    return m_display.move_caret_vertically(rows, cursor_column, m_selection, select);
}

bool editor_context::get_pos_from_screen(uint32_t x, uint32_t y, textpos_t& pos, screen_scroll_info* scroll)
{
    return m_display.get_pos_from_screen(x, y, pos, scroll);
}

bool editor_context::set_caret_from_screen(uint32_t x, uint32_t y, uint32_t drag_scroll_chars, bool word_drag)
{
    return m_display.set_caret_from_screen(x, y, m_selection, drag_scroll_chars, word_drag);
}

void editor_context::suppress_auto_horizontal_scroll()
{
    m_display.suppress_auto_horizontal_scroll(m_selection);
}

static size_t count_graphemes(const char* s, size_t len)
{
    size_t count = 0;
    wcwidth_iter iter(s, len);
    while (iter.next())
        ++count;
    return count;
}

void editor_context::clear_overwrite_input()
{
    m_overwrite_input.clear();
    m_overwrite_input_original_text.clear();
}

void editor_context::apply_message_text()
{
    if (has_numeric_argument())
    {
        static const char c_normal[] = "\x1b[m";
        cstring msg;
        msg.printf("(arg: %d) ", get_numeric_argument());
        m_display.set_message_text(msg.c_str(), uint16_t(msg.length()));
    }
    else
    {
        m_display.set_message_text(nullptr, 0);
    }
}

void editor_context::apply_override_bindings()
{
    std::shared_ptr<const key_table_list> bindings;

    if (m_numflags & NUMFLAG_ARGUMENT_MODE)
    {
        const bool uni = ((m_numflags & NUMFLAG_UNIVERSAL_MODE) &&
                          !(m_numflags & NUMFLAG_HAS_ARG_DIGITS) &&
                          m_sign_numeric_argument > 0);
        bindings = (uni ? get_universal_argument_key_table() : get_digit_argument_key_table());
    }

    override_bindings(bindings);
}

void editor_context::insert_raw_char(char c)
{
    const bool merge = (uint8_t(c) & 0xc0) == 0x80;
    begin_undo_group(merge);

    m_selection.reset_word_anchor();

    elide_selected_text();

    inc_change_counter();

    if (m_text.length() < m_max_length)
    {
        if (m_selection.get_caret() == m_text.length())
        {
            m_text.append(&c, 1);
            m_selection.set_caret(textpos_t(m_text.length()));
        }
        else
        {
            cstring tmp;
            const int32_t insert_pos = m_selection.get_caret();
            tmp.set(m_text.c_str(), insert_pos);
            tmp.append(&c, 1);
            m_selection.set_caret(textpos_t(tmp.length()));
            tmp.append(m_text.c_str() + insert_pos, m_text.length() - insert_pos);
            m_text = std::move(tmp);
        }
    }

    end_undo_group();
}

void editor_context::insert_char(char c, bool overwrite)
{
    if (!c)
        return;

    if (!overwrite)
    {
        clear_overwrite_input();
        insert_raw_char(c);
        return;
    }

    bool continuing = (!m_overwrite_input.empty() &&
                       m_overwrite_input_change_counter == m_change_counter &&
                       m_overwrite_input_navigation_counter == m_selection.get_navigation_counter() &&
                       !m_selection.has_selection());
    if (continuing)
    {
        cstring candidate(m_overwrite_input);
        candidate.append(&c, 1);
        str_iter iter(candidate.c_str(), candidate.length());
        iter.next();
        continuing = !iter.more();
    }

    if (continuing)
    {
        m_overwrite_input.append(&c, 1);
        begin_undo_group(true);
        m_text.set(m_overwrite_input_original_text);
        m_selection = m_overwrite_input_original_selection;
    }
    else
    {
        clear_overwrite_input();
        m_overwrite_input.append(&c, 1);
        m_overwrite_input_original_text.set(m_text);
        m_overwrite_input_original_selection = m_selection;
    }

    m_replaying_overwrite_input = true;
    insert_text(m_overwrite_input.c_str(), m_overwrite_input.length(), true);
    m_replaying_overwrite_input = false;

    if (continuing)
        end_undo_group();

    m_overwrite_input_change_counter = m_change_counter;
    m_overwrite_input_navigation_counter = m_selection.get_navigation_counter();
}

void editor_context::insert_text(const char* s, size_t available, bool overwrite)
{
    if (!available)
        return;

    if (!m_replaying_overwrite_input)
        clear_overwrite_input();

    available = min<size_t>(int16_max, resolve_auto_length(available, s));

    begin_undo_group();

    m_selection.reset_word_anchor();

    const bool had_selection = m_selection.has_selection();
    const textpos_t replace_begin = had_selection ? m_selection.get_sel_begin() : m_selection.get_caret();
    const textpos_t selection_end = had_selection ? m_selection.get_sel_end() : replace_begin;

    cstring base;
    base.append(m_text.c_str(), replace_begin);
    base.append(m_text.c_str() + selection_end, m_text.length() - selection_end);

    textpos_t replace_end = replace_begin;
    size_t accepted = 0;
    const textpos_t context_begin = backward_one_grapheme(base.c_str(), base.length(), replace_begin);
    cstring context(base.c_str() + context_begin, replace_begin - context_begin);
    size_t context_graphemes = count_graphemes(context.c_str(), context.length());

    wcwidth_iter iter(s, static_cast<textpos_t>(available));
    while (iter.next())
    {
        const size_t source_length = iter.character_length();
        const size_t old_context_graphemes = context_graphemes;
        context.append(s + accepted, source_length);
        context_graphemes = count_graphemes(context.c_str(), context.length());

        textpos_t candidate_end = replace_end;
        if (overwrite && !had_selection && s[accepted] != '\n')
        {
            size_t contributed = (context_graphemes > old_context_graphemes) ?
                                 context_graphemes - old_context_graphemes : 0;
            while (contributed-- && size_t(candidate_end) < base.length() && base.c_str()[candidate_end] != '\n')
                candidate_end = forward_one_grapheme(base.c_str(), base.length(), candidate_end);
        }

        const size_t candidate_length = base.length() - (candidate_end - replace_begin) + accepted + source_length;
        if (candidate_length > m_max_length)
        {
            context.set_length(context.length() - source_length);
            context_graphemes = old_context_graphemes;
            break;
        }

        accepted += source_length;
        replace_end = candidate_end;
    }

    inc_change_counter();

    cstring tmp;
    tmp.append(base.c_str(), replace_begin);
    tmp.append(s, accepted);
    m_selection.set_caret(textpos_t(tmp.length()));
    tmp.append(base.c_str() + replace_end, base.length() - replace_end);
    m_text = std::move(tmp);

    end_undo_group();
}

void editor_context::remove_text(textpos_t begin, textpos_t end)
{
    begin_undo_group();

    m_selection.reset_word_anchor();

    inc_change_counter();

    if (end < 0 || end >= m_text.length())
    {
        m_text.set_length(begin);
    }
    else
    {
        cstring tmp;
        tmp.append(m_text.c_str(), begin);
        tmp.append(m_text.c_str() + end, m_text.length() - end);
        m_text = std::move(tmp);
    }

    m_selection.set_caret(begin);

    end_undo_group();
}

bool editor_context::elide_selected_text()
{
    if (!m_selection.has_selection())
        return false;

    const textpos_t begin = m_selection.get_sel_begin();
    const textpos_t end = m_selection.get_sel_end();
    remove_text(begin, end);
    return true;
}

void editor_context::init_undo()
{
    clear_undo_internal();
    m_undo_head = m_undo_tail = new undo_entry;
    m_undo_tail->m_text = m_text;
    m_undo_tail->m_sel_before = m_selection;
    m_undo_tail->m_sel_after = m_selection;
    m_undo_tail->m_left = m_display.get_left();
    m_undo_tail->m_top = m_display.get_top();
    m_defer_init_undo = false;
}

void editor_context::clear_undo_internal()
{
    while (m_undo_head)
        unlink_endo_entry(m_undo_head);
    assert(!m_undo_head);
    assert(!m_undo_tail);
    m_undo_current = nullptr;
}

void editor_context::unlink_endo_entry(undo_entry* p)
{
    p->unlink(m_undo_head, m_undo_tail);
}

void editor_context::inc_change_counter()
{
    ++m_change_counter;
    if (!m_change_counter)
        ++m_change_counter;
}

void editor_context::begin_undo_group()
{
    begin_undo_group(false);
}

void editor_context::begin_undo_group(bool merge)
{
    if (!m_undo_head)
        return;

    assert(m_grouping >= 0);
    if (!m_grouping)
    {
        if (m_defer_init_undo)
            init_undo();

        undo_entry* current = m_undo_current ? m_undo_current : m_undo_tail;
        current->m_left = m_display.get_left();
        current->m_top = m_display.get_top();

        if (m_undo_current)
        {
            // Keep current, discard everything after current.
            m_undo_current = m_undo_current->m_next;
            while (m_undo_current)
            {
                undo_entry* del = m_undo_current;
                m_undo_current = m_undo_current->m_next;
                unlink_endo_entry(del);
                delete del;
            }
            assert(!m_undo_current);
        }

        if (!merge || m_undo_tail == m_undo_head)
        {
            undo_entry* p = new undo_entry;
            p->m_sel_before = m_selection;
            p->m_left = m_display.get_left();
            p->m_top = m_display.get_top();
            p->link_at_tail(m_undo_head, m_undo_tail);
            assert(p == m_undo_tail);
        }
    }
    ++m_grouping;
}

void editor_context::end_undo_group()
{
    if (!m_undo_head)
        return;

    assert(m_grouping > 0);
    --m_grouping;
    if (!m_grouping)
    {
        m_undo_tail->m_text.set(m_text);
        m_undo_tail->m_sel_after = m_selection;
    }
}

bool editor_context::undo()
{
    assert(!m_grouping);
    if (m_grouping)
        return false;
    if (!m_undo_head)
        return false;

    if (!m_undo_current)
        m_undo_current = m_undo_tail;
    m_undo_current->m_left = m_display.get_left();
    m_undo_current->m_top = m_display.get_top();
    undo_entry* p = m_undo_current->m_prev;
    if (!p)
        return false;

    inc_change_counter();
    m_text.set(p->m_text);
    m_selection = m_undo_current->m_sel_before;
    m_undo_current = p;
    m_display.set_scroll_offsets(p->m_left, p->m_top);
    return true;
}

bool editor_context::undo_all()
{
    assert(!m_grouping);
    if (m_grouping)
        return false;
    if (!m_undo_head)
        return false;

    undo_entry* current = m_undo_current ? m_undo_current : m_undo_tail;
    if (current == m_undo_head)
        return false;

    current->m_left = m_display.get_left();
    current->m_top = m_display.get_top();

    undo_entry* first = m_undo_head->m_next;
    assert(first);

    inc_change_counter();
    m_text.set(m_undo_head->m_text);
    m_selection = first->m_sel_before;
    m_undo_current = m_undo_head;
    m_display.set_scroll_offsets(m_undo_head->m_left, m_undo_head->m_top);
    return true;
}

bool editor_context::redo()
{
    assert(!m_grouping);
    if (m_grouping)
        return false;
    if (!m_undo_tail)
        return false;

    if (!m_undo_current || m_undo_current == m_undo_tail)
        return false;

    m_undo_current->m_left = m_display.get_left();
    m_undo_current->m_top = m_display.get_top();
    undo_entry* r = m_undo_current->m_next;
    assert(r);

    inc_change_counter();
    m_text.set(r->m_text);
    m_selection = r->m_sel_after;

    m_undo_current = r;
    m_display.set_scroll_offsets(r->m_left, r->m_top);
    return true;
}

void editor_context::transfer_text(cstring& out)
{
    out = std::move(m_text);
    initialize();
}

int32_t editor_context::dispatch(const cstring& sequence, int32_t key, const binding_target* binding, const binding_params* params) noexcept
{
    int32_t ret = -1;

    set_auto_clear_numeric_argument();

    if (binding)
    {
        switch (binding->get_type())
        {
        case binding_type::func:
            {
                const char* const name = binding->get_text();
                editor_command_func_t func = lookup_command(name);
                if (func)
                {
                    ret = func(*this, key, name, params);
                    if (ret == c_dispatch_request_quoted_insert)
                        m_quoted_insert_count = get_numeric_argument();
                }
                else
                {
                    ret = 0;
                    ding();
                }
                set_last_command(name);
            }
            break;

        case binding_type::quoted_insert:
            {
                ret = 0;
                set_last_command("self-insert");
                const char c = binding->get_char();
                int32_t repeat = m_quoted_insert_count;
                if (repeat < 0)
                {
                    repeat = 1;
                    if (++m_quoted_insert_count < 0)
                        ret = c_dispatch_request_quoted_insert;
                }
                if (!ret)
                    m_quoted_insert_count = 0;
                if (repeat > 0 && c && uint8_t(c) < c_input_terminal_reserved_begin)
                {
                    begin_undo_group();
                    while (repeat-- > 0)
                        insert_char(c, get_overwrite_mode());
                    end_undo_group();
                }
            }
            break;

        default:
            assert("unexpected binding_type" == false);
            break;
        }
    }
    else
    {
        if (is_self_insertable(key))
        {
            const char c = char(key);
            bool handled = false;
            set_last_command("self-insert");

            // The self-insert optimization collects as much raw insertable
            // input as possible into a single insert operation, requiring
            // only a single display refresh operation for the whole batch.
            //
            // However, the optimization may be turned off globally.  It may
            // also be suppressed for some scope in a specific input box, for
            // example to ensure that within some scope (perhaps for an
            // extensibility framework) any invocations of the self_insert
            // editor command insert only a single character without reading
            // any further input from the terminal.
            if (!has_numeric_argument() && g_optimize_self_insert && m_allow_optimized_self_insert)
            {
                int32_t peek = term_in_peek();
                if (is_self_insertable(peek))
                {
                    cstring input(&c, 1);
                    while (is_self_insertable(peek))
                    {
                        const int32_t cin = term_in();
                        assert(cin == peek);
                        (void)cin;
                        const char next = char(peek);
                        input.append(&next, 1);
                        peek = term_in_peek();
                    }
                    insert_text(input.c_str(), input.length(), get_overwrite_mode());
                    handled = true;
                }
            }

            if (!handled)
            {
                int32_t n = get_numeric_argument();
                if (n > 0)
                {
                    begin_undo_group();
                    while (n-- > 0)
                        insert_char(c, get_overwrite_mode());
                    end_undo_group();
                }
            }
            ret = 0;
        }
        else
        {
            ding();
        }
    }

    if (m_numflags & NUMFLAG_AUTO_CLEAR)
        clear_numeric_argument();

    if (m_auto_deactivate_mark)
        set_mark_active(false);
    else
        m_auto_deactivate_mark = true;

    return ret;
}

bool editor_context::on_binding_miss(const cstring&, int32_t) noexcept
{
    if (!(m_numflags & NUMFLAG_ARGUMENT_MODE))
        return false;

    m_numflags &= ~NUMFLAG_ARGUMENT_MODE;
    apply_override_bindings();
    apply_message_text();
    return true;
}

#ifdef DEBUG
void editor_context::dump_undo_stack()
{
    puts("");
    for (undo_entry* p = m_undo_head; p; p = p->m_next)
    {
        cstring tag;
        if (p == m_undo_head) tag.append("H");
        if (p == m_undo_tail) tag.append("T");
        if (p == m_undo_current) tag.append("C");
        printf("%s\tcaret %u/%u, anchor %u/%u, text '%s'\n", tag.c_str(), p->m_sel_before.get_caret(), p->m_sel_after.get_caret(), p->m_sel_before.get_anchor(), p->m_sel_after.get_anchor(), p->m_text.c_str());
    }
    printf("----\n");
}
#endif

} // namespace tib
