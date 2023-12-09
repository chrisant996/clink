// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>
#include <lib/line_editor.h>
#include <lib/line_buffer.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
#include <terminal/terminal_helpers.h>

#include <vector>

//------------------------------------------------------------------------------
#define DO_COMPLETE "\x09"

//------------------------------------------------------------------------------
class test_terminal_in
    : public terminal_in
{
public:
    bool                    has_input() const { return (m_read == nullptr) ? false : (*m_read != '\0'); }
    void                    set_input(const char* input) { m_input = m_read = input; }
    virtual int32           begin(bool can_hide_cursor) override { return 1; }
    virtual int32           end(bool can_show_cursor) override { return 0; }
    virtual bool            available(uint32 timeout) override { return has_input(); }
    virtual void            select(input_idle*, uint32) override {}
    virtual int32           read() override { return *(uint8*)m_read++; }
    virtual key_tester*     set_key_tester(key_tester*) override { return nullptr; }

private:
    const char*             m_input = nullptr;
    const char*             m_read = nullptr;
};

//------------------------------------------------------------------------------
class test_terminal_out
    : public terminal_out
{
public:
    virtual void            open() override {}
    virtual void            begin() override {}
    virtual void            end() override {}
    virtual void            close() override {}
    virtual void            write(const char* chars, int32 length) override {}
    virtual void            flush() override {}
    virtual int32           get_columns() const override { return 80; }
    virtual int32           get_rows() const override { return 25; }
    virtual bool            get_line_text(int32 line, str_base& out) const { return false; }
    virtual int32           is_line_default_color(int32 line) const { return true; }
    virtual int32           line_has_color(int32 line, const BYTE* attrs, int32 num_attrs, BYTE mask=0xff) const { return false; }
    virtual int32           find_line(int32 starting_line, int32 distance, const char* text, find_line_mode mode, const BYTE* attrs=nullptr, int32 num_attrs=0, BYTE mask=0xff) const { return 0; }
    virtual void            set_attributes(const attributes attr) {}
};



//------------------------------------------------------------------------------
class line_editor_tester
{
public:
                                line_editor_tester();
                                line_editor_tester(const line_editor::desc& desc, const char* command_delims, const char* word_delims);
                                ~line_editor_tester();
    line_editor*                get_editor() const;
    void                        set_input(const char* input);
    template <class ...T> void  set_expected_matches(T... t); // T must be const char*
    void                        set_expected_matches_list(const char* const* expected); // The list must be terminated with nullptr.
    void                        set_expected_classifications(const char* classifications, bool mark_argmatchers=false);
    void                        set_expected_faces(const char* faces);
    void                        set_expected_output(const char* expected);
    void                        run(bool expectationless=false);

private:
    void                        create_line_editor(const line_editor::desc* desc=nullptr);
    void                        expected_matches_impl(int32 dummy, ...);
    bool                        get_line(str_base& line);
    void                        reset_lines();

    test_terminal_in            m_terminal_in;
    test_terminal_out           m_terminal_out;
    printer*                    m_printer;
    printer_context*            m_printer_context = nullptr;
    collector_tokeniser*        m_command_tokeniser = nullptr;
    collector_tokeniser*        m_word_tokeniser = nullptr;
    std::vector<const char*>    m_expected_matches;
    str<>                       m_expected_classifications;
    str<>                       m_expected_faces;
    const char*                 m_input = nullptr;
    const char*                 m_expected_output = nullptr;
    line_editor*                m_editor = nullptr;
    bool                        m_has_matches = false;
    bool                        m_has_classifications = false;
    bool                        m_has_faces = false;
    bool                        m_mark_argmatchers = false;
};

//------------------------------------------------------------------------------
template <class ...T>
void line_editor_tester::set_expected_matches(T... t)
{
    expected_matches_impl(0, t..., nullptr);
}
