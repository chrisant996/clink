// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "char_reader_impl.h"
#include "binder.h"

#include <terminal/terminal.h>

#include <tib.h>
#include <assert.h>

enum {
    bind_id_catchall = 1,
    bind_id_codepoint_cancel,
    bind_id_number_input,
    bind_id_number_backspace,
    bind_id_number_enter,
    bind_id_number_cancel,
};

//------------------------------------------------------------------------------
static char_reader_impl* s_char_reader = nullptr;

//------------------------------------------------------------------------------
bool char_reader_impl::activate(result& result, char_reader_callback callback)
{
    if (s_char_reader != this || m_bind_group < 0 || is_active() || !callback)
        return false;

    clear_bytes();
    m_mode = mode::codepoint;
    m_callback = std::move(callback);
    m_prev_bind_group = result.set_bind_group(m_bind_group);
    g_terminal->only_simple_input(true);
    return true;
}

//------------------------------------------------------------------------------
bool char_reader_impl::activate_number(result& result, read_number_callback callback,
                                          read_number_change_callback on_change, int32 max_number)
{
    if (s_char_reader != this || m_number_bind_group < 0 || is_active() ||
        !callback || max_number < 0)
        return false;

    m_mode = mode::number;
    m_number = -1;
    m_max_number = max_number;
    m_number_callback = std::move(callback);
    m_number_change = std::move(on_change);
    m_prev_bind_group = result.set_bind_group(m_number_bind_group);
    g_terminal->only_simple_input(true);
    if (m_number_change)
        m_number_change(m_number);
    return true;
}

//------------------------------------------------------------------------------
void char_reader_impl::bind_input(binder& binder)
{
    m_bind_group = binder.create_group("char_reader");
    binder.bind(m_bind_group, "^g", bind_id_codepoint_cancel);
    binder.bind(m_bind_group, "\\e", bind_id_codepoint_cancel);
    if (const char* esc = get_bindable_esc())
        binder.bind(m_bind_group, esc, bind_id_codepoint_cancel);
    binder.bind(m_bind_group, "", bind_id_catchall);

    m_number_bind_group = binder.create_group("read_number");
    binder.bind(m_number_bind_group, "\\r", bind_id_number_enter);
    binder.bind(m_number_bind_group, "\\n", bind_id_number_enter);
    binder.bind(m_number_bind_group, "^h", bind_id_number_backspace);
    binder.bind(m_number_bind_group, "\\x7f", bind_id_number_backspace);
    binder.bind(m_number_bind_group, "^g", bind_id_number_cancel);
    if (const char* esc = get_bindable_esc())
        binder.bind(m_number_bind_group, esc, bind_id_number_cancel);
    binder.bind(m_number_bind_group, "\\e", bind_id_number_cancel);
    binder.bind(m_number_bind_group, "", bind_id_number_input);
}

//------------------------------------------------------------------------------
void char_reader_impl::on_begin_line(const context& context)
{
    assert(!s_char_reader);
    s_char_reader = this;
}

//------------------------------------------------------------------------------
void char_reader_impl::on_end_line()
{
    s_char_reader = nullptr;
    m_prev_bind_group = -1;
    finish(false);
}

//------------------------------------------------------------------------------
void char_reader_impl::on_need_input(int32& bind_group)
{
}

//------------------------------------------------------------------------------
void char_reader_impl::on_input(const input& input, result& result, const context& context)
{
    if (m_mode == mode::number)
    {
        on_number_input(input, result);
        return;
    }

    if (!m_callback)
        return;

    if (input.id == bind_id_codepoint_cancel)
    {
        result.set_bind_group(m_prev_bind_group);
        m_prev_bind_group = -1;
        finish(false);
        return;
    }

    // The catchall normally yields one byte.  Check every byte in case a
    // terminal binding later starts delivering a whole sequence.
    for (uint32 i = 0; i < input.len; ++i)
    {
        const uint8 byte = uint8(input.keys[i]);
        if (byte == 0x07 || byte == 0x1b)
        {
            result.set_bind_group(m_prev_bind_group);
            m_prev_bind_group = -1;
            finish(false);
            return;
        }

        if (!m_expected)
        {
            if (byte < 0x80)
            {
                m_bytes[0] = char(byte);
                m_length = 1;
            }
            else if (byte >= 0xc2 && byte <= 0xdf)
            {
                m_value = byte & 0x1f;
                m_min_value = 0x80;
                m_expected = 1;
            }
            else if (byte >= 0xe0 && byte <= 0xef)
            {
                m_value = byte & 0x0f;
                m_min_value = 0x800;
                m_expected = 2;
            }
            else if (byte >= 0xf0 && byte <= 0xf4)
            {
                m_value = byte & 0x07;
                m_min_value = 0x10000;
                m_expected = 3;
            }
            else
            {
                result.set_bind_group(m_prev_bind_group);
                m_prev_bind_group = -1;
                finish(false);
                return;
            }

            if (m_expected)
            {
                m_bytes[0] = char(byte);
                m_length = 1;
                continue;
            }
        }
        else
        {
            if ((byte & 0xc0) != 0x80)
            {
                result.set_bind_group(m_prev_bind_group);
                m_prev_bind_group = -1;
                finish(false);
                return;
            }
            m_bytes[m_length++] = char(byte);
            m_value = (m_value << 6) | (byte & 0x3f);
            --m_expected;
            if (m_expected)
                continue;
            if (m_value < m_min_value || m_value > 0x10ffff ||
                (m_value >= 0xd800 && m_value <= 0xdfff))
            {
                result.set_bind_group(m_prev_bind_group);
                m_prev_bind_group = -1;
                finish(false);
                return;
            }
        }

        result.set_bind_group(m_prev_bind_group);
        m_prev_bind_group = -1;
        finish(true);
        return;
    }
}

//------------------------------------------------------------------------------
void char_reader_impl::on_number_input(const input& input, result& result)
{
    switch (input.id)
    {
    case bind_id_number_enter:
    case bind_id_number_cancel:
    {
        result.set_bind_group(m_prev_bind_group);
        m_prev_bind_group = -1;
        finish(input.id == bind_id_number_enter);
        return;
    }

    case bind_id_number_backspace:
        m_number /= 10;
        if (m_number == 0)
            m_number = -1;
        break;

    case bind_id_number_input:
    {
        bool found_digit = false;
        for (uint32 i = 0; i < input.len; ++i)
        {
            const char c = input.keys[i];
            if (c >= '0' && c <= '9')
            {
                found_digit = true;
                const int32 digit = c - '0';
                const int32 number = m_number < 0 ? 0 : m_number;
                if (digit <= m_max_number && number <= (m_max_number - digit) / 10)
                    m_number = number * 10 + digit;
            }
            else
                tib::ding();
        }
        if (!found_digit)
            return;
        break;
    }

    default:
        return;
    }

    if (m_number_change)
        m_number_change(m_number);
}

//------------------------------------------------------------------------------
void char_reader_impl::on_matches_changed(const context& context, const line_state& line, const char* needle)
{
}

//------------------------------------------------------------------------------
void char_reader_impl::on_terminal_resize(int32 columns, int32 rows, const context& context)
{
}

//------------------------------------------------------------------------------
void char_reader_impl::on_signal(int32 sig)
{
    m_prev_bind_group = -1;
    finish(false);
}

//------------------------------------------------------------------------------
void char_reader_impl::finish(bool success)
{
    if (g_terminal)
        g_terminal->only_simple_input(false);

    switch (m_mode)
    {
    case mode::number:
        {
            if (!m_number_callback)
                return;

            read_number_result result;
            result.success = success;
            result.number = m_number;

            auto callback = std::move(m_number_callback);

            m_number_callback = nullptr;
            m_number_change = nullptr;
            m_mode = mode::none;

            callback(result);
        }
        break;
    case mode::codepoint:
        {
            if (!m_callback)
                return;

            char_reader_result result;
            result.success = success;
            if (success)
                result.utf8.concat_no_truncate(m_bytes, m_length);

            auto callback = std::move(m_callback);

            clear_bytes();
            m_callback = nullptr;
            m_mode = mode::none;

            callback(std::move(result));
        }
        break;
    }
}

//------------------------------------------------------------------------------
void char_reader_impl::clear_bytes()
{
    m_value = 0;
    m_min_value = 0;
    m_expected = 0;
    m_length = 0;
}

//------------------------------------------------------------------------------
bool char_reader_impl::is_active() const
{
    return m_mode != mode::none;
}

//------------------------------------------------------------------------------
bool char_reader_impl::is_codepoint_active() const
{
    return m_mode == mode::codepoint;
}

//------------------------------------------------------------------------------
bool char_reader_impl::is_number_active() const
{
    return m_mode == mode::number;
}

//------------------------------------------------------------------------------
bool is_read_codepoint_active()
{
    return s_char_reader && s_char_reader->is_codepoint_active();
}

//------------------------------------------------------------------------------
bool is_read_number_active()
{
    return s_char_reader && s_char_reader->is_number_active();
}

//------------------------------------------------------------------------------
bool is_char_reader_active()
{
    return s_char_reader && s_char_reader->is_active();
}

//------------------------------------------------------------------------------
bool read_one_codepoint(editor_module::result& result, char_reader_callback callback)
{
    if (!s_char_reader)
        return false;

    return s_char_reader->activate(result, std::move(callback));
}

//------------------------------------------------------------------------------
bool read_number(editor_module::result& result, read_number_callback callback,
                 read_number_change_callback on_change, int32 max_number)
{
    return s_char_reader && s_char_reader->activate_number(
        result, std::move(callback), std::move(on_change), max_number);
}
