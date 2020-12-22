// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "bind_resolver.h"
#include "binder.h"
#include "editor_module.h"
#include "input_dispatcher.h"
#include "terminal/key_tester.h"
#include "pager_impl.h"
#include "line_editor.h"
#include "line_state.h"
#include "matches_impl.h"
#include "word_classifier.h"
#include "rl/rl_module.h"
#include "rl/rl_buffer.h"

#include <core/array.h>
#include <terminal/printer.h>

//------------------------------------------------------------------------------
class line_editor_impl
    : public line_editor
    , public input_dispatcher
    , public key_tester
{
public:
                        line_editor_impl(const desc& desc);

    // line_editor
    virtual bool        add_module(editor_module& module) override;
    virtual bool        add_generator(match_generator& generator) override;
    virtual void        set_classifier(word_classifier& classifier) override;
    virtual bool        get_line(str_base& out) override;
    virtual bool        edit(str_base& out) override;
    virtual bool        update() override;

    // input_dispatcher
    virtual void        dispatch(int bind_group) override;

    // key_tester
    virtual bool        is_bound(const char* seq, int len) override;
    virtual bool        translate(const char* seq, int len, str_base& out) override;
    virtual void        set_keyseq_len(int len) override;

private:
    typedef editor_module                       module;
    typedef fixed_array<editor_module*, 16>     modules;
    typedef fixed_array<match_generator*, 32>   generators;
    typedef std::vector<word>                   words;
    friend matches* maybe_regenerate_matches(const char* needle, bool popup);

    enum flags : unsigned char
    {
        flag_init       = 1 << 0,
        flag_editing    = 1 << 1,
        flag_done       = 1 << 2,
        flag_eof        = 1 << 3,
    };

    void                initialise();
    void                begin_line();
    void                end_line();
    void                collect_words(bool stop_at_cursor=true);
    unsigned int        collect_words(words& words, matches_impl& matches, collect_words_mode mode);
    void                update_internal();
    bool                update_input();
    module::context     get_context(const line_state& line) const;
    line_state          get_linestate() const;
    void                set_flag(unsigned char flag);
    void                clear_flag(unsigned char flag);
    bool                check_flag(unsigned char flag) const;
    rl_module           m_module;
    rl_buffer           m_buffer;
    bool                m_has_prev_buffer = false;
    str<>               m_prev_buffer;
    desc                m_desc;
    modules             m_modules;
    generators          m_generators;
    word_classifier*    m_classifier = nullptr;
    binder              m_binder;
    bind_resolver       m_bind_resolver = { m_binder };
    words               m_words;
    word_classifications m_classifications;
    matches_impl        m_regen_matches;
    matches_impl        m_matches;
    printer&            m_printer;
    pager_impl          m_pager;
    unsigned int        m_prev_key;
    unsigned short      m_command_offset;
    unsigned char       m_keys_size;
    unsigned char       m_flags = 0;
};
