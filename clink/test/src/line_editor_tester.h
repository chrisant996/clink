// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/line_editor.h>
#include <lib/line_buffer.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>

#include <vector>

//------------------------------------------------------------------------------
#define DO_COMPLETE "\x01"

//------------------------------------------------------------------------------
class test_terminal_in
    : public terminal_in
{
public:
    bool                    has_input() const { return (m_read == nullptr) ? false : (*m_read != '\0'); }
    void                    set_input(const char* input) { m_input = m_read = input; }
    virtual void            begin() override {}
    virtual void            end() override {}
    virtual void            select() override {}
    virtual int             read() override { return *(unsigned char*)m_read++; }

private:
    const char*             m_input = nullptr;
    const char*             m_read = nullptr;
};

//------------------------------------------------------------------------------
class test_terminal_out
    : public terminal_out
{
public:
    virtual void            begin() override {}
    virtual void            end() override {}
    virtual void            write(const char* chars, int length) override {}
    virtual void            flush() override {}
    virtual int             get_columns() const override { return 80; }
    virtual int             get_rows() const override { return 25; }
    virtual void            set_attributes(const attributes attr) {}
};



//------------------------------------------------------------------------------
class line_editor_tester
{
public:
                                line_editor_tester();
                                line_editor_tester(const line_editor::desc& desc);
                                ~line_editor_tester();
    line_editor*                get_editor() const;
    void                        set_input(const char* input);
    template <class ...T> void  set_expected_matches(T... t); // T must be const char*
    void                        set_expected_output(const char* expected);
    void                        run();

private:
    void                        create_line_editor(const line_editor::desc* desc=nullptr);
    void                        expected_matches_impl(int dummy, ...);
    test_terminal_in            m_terminal_in;
    test_terminal_out           m_terminal_out;
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
