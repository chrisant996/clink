// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "test_terminals.h"

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
class clipboard_tester
    : public os::clipboard_provider, public singleton<clipboard_tester>
{
public:
                clipboard_tester() { os::set_clipboard_provider(this); }
                ~clipboard_tester() { os::set_clipboard_provider(nullptr); }
    bool        get_clipboard_text(str_base& out) override;
    bool        set_clipboard_text(const char* text, int32 length) override;
private:
    wstr_moveable m_text;
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
    template <class ...T> void  set_expected_words(T... t); // T must be const char*
    void                        set_expected_words_list(const char* const* expected); // The list must be terminated with nullptr.
    void                        set_expected_classifications(const char* classifications, bool mark_argmatchers=false);
    void                        set_expected_faces(const char* faces);
    void                        set_expected_hint(const char* expected);
    void                        set_expected_output(const char* expected);
    void                        set_tab_binding(const char* tab_binding=nullptr);
    void                        run(bool expectationless=false);

private:
    void                        create_line_editor(const line_editor::desc* desc=nullptr);
    void                        expected_matches_impl(int32 dummy, ...);
    void                        expected_words_impl(int32 dummy, ...);
    bool                        get_line(str_base& line);
    void                        reset_lines();

    test_terminal_in            m_terminal_in;
    test_terminal_out           m_terminal_out;
    printer*                    m_printer;
    printer_context*            m_printer_context = nullptr;
    collector_tokeniser*        m_command_tokeniser = nullptr;
    collector_tokeniser*        m_word_tokeniser = nullptr;
    clipboard_tester            m_clipboard_tester;
    std::vector<const char*>    m_expected_matches;
    std::vector<const char*>    m_expected_words;
    str<>                       m_expected_classifications;
    str<>                       m_expected_faces;
    const char*                 m_expected_hint = nullptr;
    const char*                 m_input = nullptr;
    const char*                 m_expected_output = nullptr;
    line_editor*                m_editor = nullptr;
    bool                        m_has_matches = false;
    bool                        m_has_words = false;
    bool                        m_has_classifications = false;
    bool                        m_has_faces = false;
    bool                        m_has_hint = false;
    bool                        m_mark_argmatchers = false;
    const char*                 m_tab_binding = nullptr;
};

//------------------------------------------------------------------------------
template <class ...T>
void line_editor_tester::set_expected_matches(T... t)
{
    expected_matches_impl(0, t..., nullptr);
}

//------------------------------------------------------------------------------
template <class ...T>
void line_editor_tester::set_expected_words(T... t)
{
    expected_words_impl(0, t..., nullptr);
}
