// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>
#include <lib/bind_resolver.h>
#include <lib/binder.h>
#include <lib/editor_backend.h>
#include <lib/line_state.h>
#include <lib/matches.h>

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
