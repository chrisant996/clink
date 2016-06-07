// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/line_buffer.h>
#include <terminal/terminal.h>

#include <vector>

class line_editor;

//------------------------------------------------------------------------------
class test_termbuf
    : public terminal
    , public line_buffer
{
public:
    bool                    has_input() const { return (m_read == nullptr) ? false : (*m_read != '\0'); }
    void                    set_input(const char* input) { m_input = m_read = input; } 

    /* terminal_in */
    virtual void            select() override {}
    virtual int             read() override { return *(unsigned char*)m_read++; }

    /* terminal_out */
    virtual void            write(const char* chars, int length) override {}
    virtual void            flush() override {}
    virtual int             get_columns() const override { return 80; }
    virtual int             get_rows() const override { return 25; }

    /* terminal */
    virtual void            begin() override {}
    virtual void            end() override {}

    /* line_buffer */
    virtual const char*     get_buffer() const override { return m_input; }
    virtual unsigned int    get_cursor() const override { return int(m_read - m_input); }
    virtual unsigned int    set_cursor(unsigned int pos) override { return pos; }
    virtual bool            insert(const char* text) override { return true; }
    virtual bool            remove(unsigned int from, unsigned int to) override { return true; }
    virtual void            draw() override {}
    virtual void            redraw() override {}

private:
    const char*             m_input = nullptr;
    const char*             m_read = nullptr;
};



//------------------------------------------------------------------------------
class line_editor_tester
{
public:
                                line_editor_tester();
    line_editor*                get_editor() const;
    void                        set_input(const char* input);
    template <class ...T> void  set_expected_matches(T... t); // T must be const char*
    void                        set_expected_output(const char* expected);
    void                        run();

private:
    void                        expected_matches_impl(int dummy, ...);
    test_termbuf                m_test_termbuf;
    std::vector<const char*>    m_expected_matches;
    const char*                 m_input = nullptr;
    const char*                 m_expected_output = nullptr;
    line_editor*                m_editor = nullptr;
    bool                        m_has_matches = false;
};

//------------------------------------------------------------------------------
template <class ...T>
void line_editor_tester::set_expected_matches(T... t)
{
    expected_matches_impl(0, t..., nullptr);
}
