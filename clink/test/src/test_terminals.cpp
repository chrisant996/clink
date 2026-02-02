// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "test_terminals.h"
#include <assert.h>

class input_idle;
class key_tester;

//------------------------------------------------------------------------------
int32 test_terminal_in::begin(bool /*can_hide_cursor*/)
{
    ++m_began;
    return m_began;
}

//------------------------------------------------------------------------------
int32 test_terminal_in::end(bool /*can_show_cursor*/)
{
    assert(m_began > 0);
    --m_began;
    return m_began;
}

//------------------------------------------------------------------------------
bool test_terminal_in::available(uint32 timeout)
{
    return m_head < m_queue.size();
}

//------------------------------------------------------------------------------
void test_terminal_in::select(input_idle* /*callback*/, uint32 /*timeout*/)
{
    // The test_terminal_in never waits for user input, so there's nothing for
    // this function to do.
}

//------------------------------------------------------------------------------
int32 test_terminal_in::read()
{
    if (m_head >= m_queue.size())
        return terminal_in::input_none;
    const int32 c = m_queue[m_head++];
    return c;
}

//------------------------------------------------------------------------------
int32 test_terminal_in::peek()
{
    if (m_head >= m_queue.size())
        return terminal_in::input_none;
    return m_queue[m_head];
}

//------------------------------------------------------------------------------
bool test_terminal_in::send_terminal_request(const char* request, const char* pattern, str_base& out)
{
    assert(false);
    out.clear();
    return false;
}

//------------------------------------------------------------------------------
key_tester* test_terminal_in::set_key_tester(key_tester* keys)
{
    key_tester* ret = m_keys;
    m_keys = keys;
    return ret;
}

//------------------------------------------------------------------------------
void test_terminal_in::set_input(const char* input, int32 length)
{
    m_queue.clear();
    m_head = 0;
    push_input(input, length);
}

//------------------------------------------------------------------------------
void test_terminal_in::push_input(const char* input, int32 length)
{
    if (length < 0)
        length = str_len(input);

    if (length > 0)
    {
        const size_t pos = m_queue.size();
        m_queue.resize(m_queue.size() + length);
        memcpy(&m_queue.front() + pos, input, length);
    }
}



//------------------------------------------------------------------------------
bool test_terminal_out::get_line_text(int32 line, str_base& out) const
{
    if (line < 0 || line >= get_rows())
        return false;

    out.clear();
    if (size_t(line) < m_lines.size())
        out = m_lines[line].c_str();
    return true;
}

//------------------------------------------------------------------------------
void test_terminal_out::set_line_text(int32 line, const char* text)
{
    assert(line >= 0);
    if (line < 0)
        return;

    while (size_t(line) >= m_lines.size())
        m_lines.emplace_back();

    m_lines[line] = text;
}
