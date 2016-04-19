// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>
#include <line_state.h>
#include <lib/binder.h>
#include <lib/bind_resolver.h>
#include <lib/editor_backend.h>
#include <matches/matches.h>

class editor_backend;
class line_buffer;
class match_generator;
class terminal;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        const char*     prompt;
        const char*     command_delims; // MODE4
        const char*     quote_pair;
        const char*     word_delims;
        const char*     partial_delims;
        terminal*       terminal;
        editor_backend* backend;
        line_buffer*    buffer;
    };

                        line_editor(const desc& desc);
    bool                add_backend(editor_backend& backend);
    bool                add_generator(match_generator& generator);
    bool                get_line(char* out, int out_size);
    bool                edit(char* out, int out_size);
    bool                update();

private:
    typedef editor_backend                      backend;
    typedef fixed_array<editor_backend*, 16>    backends;
    typedef fixed_array<match_generator*, 32>   generators;
    typedef fixed_array<word, 72>               words;

    void                initialise();
    void                begin_line();
    void                end_line();
    void                collect_words();
    void                update_internal();
    void                record_input(unsigned char key);
    void                dispatch();
    void                accept_match(unsigned int index);
    backend::context    make_context(const line_state& line) const;
    char                m_keys[8];
    desc                m_desc;
    backends            m_backends;
    generators          m_generators;
    binder              m_binder;
    bind_resolver       m_bind_resolver;
    words               m_words;
    matches             m_matches;
    unsigned int        m_prev_key;
    unsigned char       m_keys_size;
    bool                m_begun;
    bool                m_initialised;
};

/* MODE4
#include <core/str.h>

class match_printer;
class matches;
class terminal;

//------------------------------------------------------------------------------
class line_editor
{
public:
    struct desc
    {
        const char*         shell_name;
        terminal*           term;
        match_printer*      match_printer;
    };

                            line_editor(const desc& desc);
                            line_editor(const line_editor&) = delete;
    void                    operator = (const line_editor&) = delete;
    virtual                 ~line_editor();
    virtual bool            edit_line(const char* prompt, str_base& out) = 0;
    terminal*               get_terminal() const;
    match_printer*          get_match_printer() const;
    const char*             get_shell_name() const;

private:
    terminal*               m_terminal;
    match_printer*          m_match_printer;
    str<32>                 m_shell_name;

private:
};

//------------------------------------------------------------------------------
inline line_editor::line_editor(const desc& desc)
: m_terminal(desc.term)
, m_match_printer(desc.match_printer)
{
    m_shell_name << desc.shell_name;
}

//------------------------------------------------------------------------------
inline line_editor::~line_editor()
{
}

//------------------------------------------------------------------------------
inline terminal* line_editor::get_terminal() const
{
    return m_terminal;
}

//------------------------------------------------------------------------------
inline match_printer* line_editor::get_match_printer() const
{
    return m_match_printer;
}

//------------------------------------------------------------------------------
inline const char* line_editor::get_shell_name() const
{
    return m_shell_name.c_str();
}
MODE4 */
