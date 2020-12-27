// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "line_editor_impl.h"
#include "line_buffer.h"
#include "match_generator.h"
#include "match_pipeline.h"
#include "pager.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
}

//------------------------------------------------------------------------------
const int simple_input_states = (RL_STATE_MOREINPUT |
                                 RL_STATE_NSEARCH |
                                 RL_STATE_CHARSEARCH);



//------------------------------------------------------------------------------
inline char get_closing_quote(const char* quote_pair)
{
    return quote_pair[1] ? quote_pair[1] : quote_pair[0];
}

//------------------------------------------------------------------------------
static bool find_func_in_keymap(str_base& out, rl_command_func_t *func, Keymap map)
{
    for (int key = 0; key < KEYMAP_SIZE; key++)
    {
        switch (map[key].type)
        {
        case ISMACR:
            break;
        case ISFUNC:
            if (map[key].function == func)
            {
                char ch = char((unsigned char)key);
                out.concat(&ch, 1);
                return true;
            }
            break;
        case ISKMAP:
            {
                unsigned int old_len = out.length();
                char ch = char((unsigned char)key);
                out.concat(&ch, 1);
                if (find_func_in_keymap(out, func, FUNCTION_TO_KEYMAP(map, key)))
                    return true;
                out.truncate(old_len);
            }
            break;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
static bool find_abort_in_keymap(str_base& out)
{
    rl_command_func_t *func = rl_named_function("abort");
    if (!func)
        return false;

    Keymap map = rl_get_keymap();
    return find_func_in_keymap(out, func, map);
}



//------------------------------------------------------------------------------
line_editor* line_editor_create(const line_editor::desc& desc)
{
    // Check there's at least a terminal and a printer.
    if (desc.input == nullptr)
        return nullptr;

    if (desc.output == nullptr)
        return nullptr;

    if (desc.printer == nullptr)
        return nullptr;

    return new line_editor_impl(desc);
}

//------------------------------------------------------------------------------
void line_editor_destroy(line_editor* editor)
{
    delete editor;
}



//------------------------------------------------------------------------------
static line_editor_impl* s_editor = nullptr;

//------------------------------------------------------------------------------
line_editor_impl::line_editor_impl(const desc& desc)
: m_module(desc.shell_name, desc.input)
, m_desc(desc)
, m_buffer(desc.command_delims, desc.word_delims, desc.get_quote_pair())
, m_regen_matches(&m_generators)
, m_matches(&m_generators)
, m_printer(*desc.printer)
, m_pager(*this)
{
    add_module(m_module);
    add_module(m_pager);

    desc.input->set_key_tester(this);
}

//------------------------------------------------------------------------------
void line_editor_impl::initialise()
{
    if (check_flag(flag_init))
        return;

    struct : public editor_module::binder {
        virtual int get_group(const char* name) const override
        {
            return binder->get_group(name);
        }

        virtual int create_group(const char* name) override
        {
            return binder->create_group(name);
        }

        virtual bool bind(unsigned int group, const char* chord, unsigned char key) override
        {
            return binder->bind(group, chord, *module, key);
        }

        ::binder*       binder;
        editor_module*  module;
    } binder_impl;

    binder_impl.binder = &m_binder;
    for (auto* module : m_modules)
    {
        binder_impl.module = module;
        module->bind_input(binder_impl);
    }

    set_flag(flag_init);
}

//------------------------------------------------------------------------------
void line_editor_impl::begin_line()
{
    clear_flag(~flag_init);
    set_flag(flag_editing);

    m_bind_resolver.reset();
    m_command_offset = 0;
    m_keys_size = 0;
    m_prev_key = ~0u;

    assert(!s_editor);
    s_editor = this;

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    m_desc.input->begin();
    m_desc.output->begin();
    m_buffer.begin_line();
    m_has_prev_buffer = false;
    m_prev_buffer.clear();

    line_state line = get_linestate();
    editor_module::context context = get_context(line);
    for (auto module : m_modules)
        module->on_begin_line(context);
}

//------------------------------------------------------------------------------
void line_editor_impl::end_line()
{
    for (auto i = m_modules.rbegin(), n = m_modules.rend(); i != n; ++i)
        i->on_end_line();

    m_buffer.end_line();
    m_desc.output->end();
    m_desc.input->end();

    s_editor = nullptr;

    clear_flag(flag_editing);
}

//------------------------------------------------------------------------------
bool line_editor_impl::add_module(editor_module& module)
{
    editor_module** slot = m_modules.push_back();
    return (slot != nullptr) ? *slot = &module, true : false;
}

//------------------------------------------------------------------------------
bool line_editor_impl::add_generator(match_generator& generator)
{
    match_generator** slot = m_generators.push_back();
    return (slot != nullptr) ? *slot = &generator, true : false;
}

void line_editor_impl::set_classifier(word_classifier& classifier)
{
    m_classifier = &classifier;
}

//------------------------------------------------------------------------------
bool line_editor_impl::get_line(str_base& out)
{
    if (check_flag(flag_editing))
        end_line();

    if (check_flag(flag_eof))
        return false;

    const char* line = m_buffer.get_buffer();
    out.copy(line);
    return true;
}

//------------------------------------------------------------------------------
bool line_editor_impl::edit(str_base& out)
{
    // Update first so the init state goes through.
    while (update())
        m_desc.input->select();

    return get_line(out);
}

//------------------------------------------------------------------------------
bool line_editor_impl::update()
{
    if (!check_flag(flag_init))
        initialise();

    if (!check_flag(flag_editing))
    {
        begin_line();
        update_internal();
        return true;
    }

    update_input();

    if (!check_flag(flag_editing))
        return false;

    update_internal();
    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::dispatch(int bind_group)
{
    assert(check_flag(flag_init));
    assert(check_flag(flag_editing));

    // Handle one input.

    const int prev_bind_group = m_bind_resolver.get_group();
    m_bind_resolver.set_group(bind_group);

// TODO: is this really a viable approach?
    do
    {
        m_desc.input->select();
    }
    while (!update());

    assert(check_flag(flag_editing));

    m_bind_resolver.set_group(prev_bind_group);
}

//------------------------------------------------------------------------------
bool line_editor_impl::is_bound(const char* seq, int len)
{
    if (!len)
    {
LNope:
        rl_ding();
        return false;
    }

    // `quoted-insert` must accept all input (that's its whole purpose).
    if (rl_is_insert_next_callback_pending())
        return true;

    // Various states should only accept "simple" input, i.e. not CSI sequences,
    // so that unrecognized portions of key sequences don't bleed in as textual
    // input.
    if (RL_ISSTATE(simple_input_states))
    {
        if (seq[0] == '\x1b')
            goto LNope;
        return true;
    }

    // Eventually this will go away, but for now check if clink has a binding.
    if (m_bind_resolver.is_bound(seq, len))
        return true;

    // The intent here is to accept all UTF8 input (not sure why readline
    // reports them as not bound, but this seems good enough for now).
    if (len > 1 && (unsigned char)seq[0] >= ' ')
        return true;

    // Checking readline's keymap is incorrect when a special bind group is
    // active that should block on_input from reaching readline.  But the way
    // that blocking is achieved is by adding a "" binding that matches
    // everything not explicitly bound in the keymap.  So it works out
    // naturally, without additional effort.
    if (rl_function_of_keyseq_len(seq, len, nullptr, nullptr))
        return true;

    goto LNope;
}

//------------------------------------------------------------------------------
bool line_editor_impl::translate(const char* seq, int len, str_base& out)
{
    if (RL_ISSTATE(RL_STATE_NUMERICARG))
    {
        if (strcmp(seq, bindableEsc) == 0)
        {
            // Let ESC terminate numeric arg mode (digit mode) by redirecting it
            // to 'abort'.
            if (find_abort_in_keymap(out))
                return true;
        }
    }
    else if (RL_ISSTATE(RL_STATE_ISEARCH|RL_STATE_NSEARCH))
    {
        if (strcmp(seq, bindableEsc) == 0)
        {
            // These modes have hard-coded handlers that abort on Ctrl+G, so
            // redirect ESC to Ctrl+G.
            char tmp[2] = { ABORT_CHAR };
            out = tmp;
            return true;
        }
    }
    else if (RL_ISSTATE(simple_input_states) || rl_is_insert_next_callback_pending())
    {
        if (strcmp(seq, bindableEsc) == 0)
        {
            out = "\x1b";
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
void line_editor_impl::set_keyseq_len(int len)
{
    m_module.set_keyseq_len(len);
}

//------------------------------------------------------------------------------
// Returns false when a chord is in progress, otherwise returns true.  This is
// to help dispatch() be able to dispatch an entire chord.
bool line_editor_impl::update_input()
{
    int key = m_desc.input->read();

    if (key == terminal_in::input_terminal_resize)
    {
        int columns = m_desc.output->get_columns();
        int rows = m_desc.output->get_rows();
        line_state line = get_linestate();
        editor_module::context context = get_context(line);
        for (auto* module : m_modules)
            module->on_terminal_resize(columns, rows, context);
    }

    if (key == terminal_in::input_abort)
    {
        m_buffer.reset();
        end_line();
        return true;
    }

    if (key < 0)
        return true;

    if (!m_bind_resolver.step(key))
        return false;

    struct result_impl : public editor_module::result
    {
        enum
        {
            flag_pass       = 1 << 0,
            flag_done       = 1 << 1,
            flag_eof        = 1 << 2,
            flag_redraw     = 1 << 3,
        };

        virtual void    pass() override                           { flags |= flag_pass; }
        virtual void    done(bool eof) override                   { flags |= flag_done|(eof ? flag_eof : 0); }
        virtual void    redraw() override                         { flags |= flag_redraw; }
        virtual int     set_bind_group(int id) override           { int t = group; group = id; return t; }
        unsigned short  group;  //        <! MSVC bugs; see connect
        unsigned char   flags;  // = 0;   <! issues about C2905
    };

// input_dispatcher::dispatch()?
    while (auto binding = m_bind_resolver.next())
    {
        // Binding found, dispatch it off to the module.
        result_impl result;
        result.flags = 0;
        result.group = m_bind_resolver.get_group();

        str<16> chord;
        editor_module* module = binding.get_module();
        unsigned char id = binding.get_id();
        binding.get_chord(chord);

        line_state line = get_linestate();
        editor_module::context context = get_context(line);
        editor_module::input input = { chord.c_str(), chord.length(), id };
        module->on_input(input, result, context);

        m_bind_resolver.set_group(result.group);

        // Process what result_impl has collected.
        if (result.flags & result_impl::flag_pass)
            continue;

        binding.claim();

        if (result.flags & result_impl::flag_done)
        {
            end_line();

            if (result.flags & result_impl::flag_eof)
                set_flag(flag_eof);
        }

        if (!check_flag(flag_editing))
            return true;

        if (result.flags & result_impl::flag_redraw)
            m_buffer.redraw();
    }

    m_buffer.draw();
    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::collect_words(bool stop_at_cursor)
{
    collect_words_mode mode = stop_at_cursor ? collect_words_mode::stop_at_cursor : collect_words_mode::whole_command;
    m_command_offset = collect_words(m_words, m_matches, mode);
}

//------------------------------------------------------------------------------
unsigned int line_editor_impl::collect_words(words& words, matches_impl& matches, collect_words_mode mode)
{
    unsigned int command_offset = m_buffer.collect_words(words, mode);

    // The last word can be split by the match generators, to influence word
    // breaks. This is a little clunky but works well enough.
    line_state line { m_buffer.get_buffer(), m_buffer.get_cursor(), command_offset, words };
    word* end_word = &words.back();
    if (end_word->length && mode == collect_words_mode::stop_at_cursor)
    {
        word_break_info break_info = {};
        const char *word_start = m_buffer.get_buffer() + end_word->offset;
        for (const auto *generator : m_generators)
        {
            word_break_info tmp;
            generator->get_word_break_info(line, tmp);
            if ((tmp.truncate > break_info.truncate) ||
                (tmp.truncate == break_info.truncate && tmp.keep > break_info.keep))
                break_info = tmp;
        }
        if (break_info.truncate)
        {
            int truncate = min<unsigned int>(break_info.truncate, end_word->length);

            word split_word;
            split_word.offset = end_word->offset + truncate;
            split_word.length = end_word->length - truncate;
            split_word.command_word = false;
            split_word.quoted = false;
            split_word.delim = str_token::invalid_delim;

            end_word->length = truncate;
            words.push_back(split_word);
            end_word = &words.back();
        }

        int keep = min<unsigned int>(break_info.keep, end_word->length);
        end_word->length = keep;

        // Need to coordinate with Readline when we redefine word breaks.
        matches.set_word_break_adjustment(break_info.truncate);
    }

    return command_offset;
}

//------------------------------------------------------------------------------
line_state line_editor_impl::get_linestate() const
{
    return {
        m_buffer.get_buffer(),
        m_buffer.get_cursor(),
        m_command_offset,
        m_words,
    };
}

//------------------------------------------------------------------------------
editor_module::context line_editor_impl::get_context(const line_state& line) const
{
    auto& pter = const_cast<printer&>(m_printer);
    auto& pger = const_cast<pager&>(static_cast<const pager&>(m_pager));
    auto& buffer = const_cast<rl_buffer&>(m_buffer);
    return { m_desc.prompt, pter, pger, buffer, line, m_matches, m_classifications };
}

//------------------------------------------------------------------------------
void line_editor_impl::set_flag(unsigned char flag)
{
    m_flags |= flag;
}

//------------------------------------------------------------------------------
void line_editor_impl::clear_flag(unsigned char flag)
{
    m_flags &= ~flag;
}

//------------------------------------------------------------------------------
bool line_editor_impl::check_flag(unsigned char flag) const
{
    return ((m_flags & flag) != 0);
}

//------------------------------------------------------------------------------
void line_editor_impl::update_internal()
{
    // Parse word types for coloring the input line.
    // TODO: Would a better place would be inside the `rl_redisplay_function` replacement?
    if (m_classifier && (!m_has_prev_buffer || !m_prev_buffer.equals(m_buffer.get_buffer())))
    {
        m_has_prev_buffer = true;
        m_prev_buffer = m_buffer.get_buffer();

        // Use the full line; don't stop at the cursor.
        line_state line = get_linestate();
        collect_words(false);

        m_classifications.clear();

        int i = 0;
        std::vector<word> words;
        while (true)
        {
            if (!words.empty() && (i >= m_words.size() || m_words[i].command_word))
            {
                line_state linestate(
                    m_buffer.get_buffer(),
                    m_buffer.get_cursor(),
                    words[0].offset,
                    words
                );
                m_classifier->classify(linestate, m_classifications);
                words.clear();
            }

            if (i >= m_words.size())
                break;

            words.push_back(m_words[i]);
            i++;
        }

#ifdef DEBUG
        if (dbg_get_env_int("DEBUG_CLASSIFY"))
        {
            static const char *const word_class_name[] = {"other", "command", "doskey", "arg", "flag", "none"};
            printf("CLASSIFIED '%s' --", m_buffer.get_buffer());
            for (auto c : m_classifications)
                printf(" %s", word_class_name[int(c)]);
            printf("\n");
        }
#endif

        // Tell all the modules that the classifications changed.
        editor_module::context context = get_context(line);
        assert(&context.classifications);
        for (auto module : m_modules)
            module->on_classifications_changed(context);
    }

    // Collect words, stopping at the cursor for match generator.
    collect_words();

    const word& end_word = m_words.back();

    union key_t {
        struct {
            unsigned int word_offset : 11;
            unsigned int word_length : 10;
            unsigned int cursor_pos  : 11;
        };
        unsigned int value;
    };

    key_t next_key = { end_word.offset, end_word.length };

    key_t prev_key;
    prev_key.value = m_prev_key;
    prev_key.cursor_pos = 0;

    // Should we generate new matches?
    if (next_key.value != prev_key.value)
    {
        line_state line = get_linestate();
        match_pipeline pipeline(m_matches);
        pipeline.reset();
        pipeline.generate(line, m_generators);
    }

    next_key.cursor_pos = m_buffer.get_cursor();
    prev_key.value = m_prev_key;

    // Should we sort and select matches?
    if (next_key.value != prev_key.value)
    {
        str<64> needle;
        int needle_start = end_word.offset;
        const char* buf_ptr = m_buffer.get_buffer();
        needle.concat(buf_ptr + needle_start, next_key.cursor_pos - needle_start);

        if (!needle.empty() && end_word.quoted)
        {
            int i = needle.length();
            if (needle[i - 1] == get_closing_quote(m_desc.get_quote_pair()))
                needle.truncate(i - 1);
        }

        match_pipeline pipeline(m_matches);
        pipeline.select(needle.c_str());
        pipeline.sort();

        m_prev_key = next_key.value;

        // Tell all the modules that the matches changed.
        line_state line = get_linestate();
        editor_module::context context = get_context(line);
        for (auto module : m_modules)
            module->on_matches_changed(context);
    }
}

matches* maybe_regenerate_matches(const char* needle, bool popup)
{
    if (!s_editor)
        return nullptr;

    // Check if a match display filter is active.
    matches_impl& regen = s_editor->m_regen_matches;
    if (!regen.match_display_filter(nullptr, nullptr, popup))
        return nullptr;

#ifdef DEBUG
    int debug_filter = dbg_get_env_int("DEBUG_FILTER");
    if (debug_filter) puts("REGENERATE_MATCHES");
#endif

    std::vector<word> words;
    unsigned int command_offset = s_editor->collect_words(words, regen, collect_words_mode::display_filter);
    line_state line
    {
        s_editor->m_buffer.get_buffer(),
        s_editor->m_buffer.get_cursor(),
        command_offset,
        words,
    };

    match_pipeline pipeline(regen);
    pipeline.reset();

#ifdef DEBUG
    if (debug_filter) puts("-- GENERATE");
#endif

    pipeline.generate(line, s_editor->m_generators);

#ifdef DEBUG
    if (debug_filter) puts("-- SELECT");
#endif

    pipeline.select(needle);
    pipeline.sort();

#ifdef DEBUG
    if (debug_filter)
    {
        matches_iter iter = regen.get_iter();
        while (iter.next())
            printf("match '%s'\n", iter.get_match());
        puts("-- DONE");
    }
#endif

    return &regen;
}
