// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "line_editor.h"
#include "bind_resolver.h"
#include "binder.h"
#include "editor_backend.h"
#include "line_state.h"
#include "matches_impl.h"

#include <core/array.h>

//------------------------------------------------------------------------------
class line_editor_impl
    : public line_editor
{
public:
                        line_editor_impl(const desc& desc);
    virtual bool        add_backend(editor_backend& backend) override;
    virtual bool        add_generator(match_generator& generator) override;
    virtual bool        get_line(char* out, int out_size) override;
    virtual bool        edit(char* out, int out_size) override;
    virtual bool        update() override;

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
    matches_impl        m_matches;
    unsigned int        m_prev_key;
    unsigned char       m_keys_size;
    bool                m_begun;
    bool                m_initialised;
};
