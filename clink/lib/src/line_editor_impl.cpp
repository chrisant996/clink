// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <assert.h>
#include "line_editor_impl.h"
#include "line_buffer.h"
#include "match_generator.h"
#include "match_pipeline.h"
#include "pager.h"
#include "host_callbacks.h"
#include "reclassify.h"
#include "cmd_tokenisers.h"
#include "doskey.h"
#include "display_readline.h"
#include "clink_ctrlevent.h"
#include "clink_rl_signal.h"
#include "line_editor_integration.h"
#include "suggestions.h"
#include "recognizer.h"
#include "hinter.h"

#ifdef DEBUG
#include "ellipsify.h"
#include "terminal/ecma48_iter.h"
#endif

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <core/settings.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
#include <terminal/input_idle.h>
#include <rl/rl_commands.h>
extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
#include <readline/history.h>
}

//------------------------------------------------------------------------------
static setting_bool s_comment_row_show_hints(
    "comment_row.show_hints",
    "Allow showing input hints in the comment row",
    false);

extern setting_bool g_classify_words;
extern setting_bool g_autosuggest_async;
extern setting_bool g_autosuggest_enable;
extern setting_bool g_history_autoexpand;
extern setting_enum g_expand_mode;
extern setting_bool g_history_show_preview;
extern setting_enum g_default_bindings;
extern setting_color g_color_histexpand;
// TODO: line_editor_impl vs rl_module.
extern int32 g_suggestion_offset;
void before_display_readline();



//------------------------------------------------------------------------------
inline char get_closing_quote(const char* quote_pair)
{
    return quote_pair[1] ? quote_pair[1] : quote_pair[0];
}

//------------------------------------------------------------------------------
static bool rl_vi_insert_mode_esc_special_case(int32 key)
{
    // This mirrors the conditions in the #if defined (VI_MODE) block in
    // _rl_dispatch_subseq() in readline.c.  This is so when `terminal.raw_esc`
    // is set the timeout hack can work for ESC in vi insertion mode.

    if (rl_editing_mode == vi_mode &&
        key == ESC &&
        _rl_keymap == vi_insertion_keymap &&
        _rl_keymap[key].type == ISKMAP &&
        (FUNCTION_TO_KEYMAP(_rl_keymap, key))[ANYOTHERKEY].type == ISFUNC)
    {
        if ((RL_ISSTATE(RL_STATE_INPUTPENDING|RL_STATE_MACROINPUT) == 0) &&
            _rl_pushed_input_available() == 0 &&
            _rl_input_queued(0) == 0)
            return true;

        if ((RL_ISSTATE (RL_STATE_INPUTPENDING) == 0) &&
            (RL_ISSTATE (RL_STATE_MACROINPUT) && _rl_peek_macro_key() == 0) &&
            _rl_pushed_input_available() == 0 &&
            _rl_input_queued(0) == 0)
            return true;
    }

    return false;
}



//------------------------------------------------------------------------------
line_editor* line_editor_create(const line_editor::desc& desc)
{
    if (desc.input == nullptr) return nullptr;
    if (desc.output == nullptr) return nullptr;
    if (desc.printer == nullptr) return nullptr;

    return new line_editor_impl(desc);
}

//------------------------------------------------------------------------------
void line_editor_destroy(line_editor* editor)
{
    delete editor;
}



//------------------------------------------------------------------------------
void prev_buffer::set(const char* s, int32 len)
{
    free(m_ptr);

    m_ptr = (char*)malloc(len + 1);
    m_len = len;
    memcpy(m_ptr, s, len);
    m_ptr[len] = '\0';
}

//------------------------------------------------------------------------------
bool prev_buffer::equals(const char* s, int32 len) const
{
    return m_ptr && m_len == uint32(len) && memcmp(s, m_ptr, len) == 0 && !m_ptr[len];
}



//------------------------------------------------------------------------------
static void calc_history_expansions(const line_buffer& buffer, history_expansion*& list)
{
    list = nullptr;

    if (!g_history_autoexpand.get() || !g_expand_mode.get())
        return;

    const char* color = g_color_histexpand.get();
    if (!g_history_show_preview.get() && !(color && (*color)))
        return;

    // Counteract auto-suggestion, but restore it afterwards.
    char* p = const_cast<char*>(buffer.get_buffer());
    rollback<char> rb(p[buffer.get_length()], '\0');

    {
        // The history expansion library can have side effects on the global
        // history variables.  Must save and restore them.
        save_history_expansion_state();

        // Reset history offset so expansion is always relative to the end of
        // the history list.
        using_history();

        // Perform history expansion.
        char* output = nullptr;
        history_return_expansions = true;
        history_expand(p, &output);
        free(output);

        // Restore global history variables.
        restore_history_expansion_state();
    }

    list = history_expansions;
    history_expansions = nullptr;
}

//------------------------------------------------------------------------------
static void classify_history_expansions(const history_expansion* list, word_classifications& classifications)
{
    if (!list)
        return;

    if (is_null_or_empty(g_color_histexpand.get()))
        return;

    for (const history_expansion* e = list; e; e = e->next)
        classifications.apply_face(e->start, e->len, FACE_HISTEXPAND, true);
}



//------------------------------------------------------------------------------
static bool is_endword_tilde(const line_state& line)
{
    // Tilde expansion is quirky:
    //  - Tilde by itself should generate completions.
    //  - Tilde followed by a path separator should generate completions.
    //  - Tilde followed by anything else should not.
    //  - Tilde by itself should not list possible completions.
    // Any time the end word is just a tilde it means it could be a bare tilde
    // or a tilde followed by something other than a path separator.  Since that
    // has conditional behavior, it violates the principle that generators
    // produce consistent results without being influenced by the word prefix.
    // Therefore it's necessary to reset match generation so subsequent
    // completions can work.

    bool tilde = false;
    for (uint32 offset = line.get_end_word_offset(); true; ++offset)
    {
        if (offset > line.get_cursor())
            break;
        if (offset == line.get_cursor())
            return tilde;

        char c = line.get_line()[offset];
        if (c == '"')
            continue;
        if (c == '~' && !tilde)
        {
            tilde = true;
            continue;
        }
        break;
    }

    return false;
}

//------------------------------------------------------------------------------
line_editor_impl::line_editor_impl(const desc& desc)
: m_desc(desc)
, m_module(desc.input)
, m_collector(desc.command_tokeniser, desc.word_tokeniser, desc.get_quote_pair())
, m_printer(*desc.printer)
, m_pager(*this)
, m_selectcomplete(*this)
, m_textlist(*this)
{
    add_module(m_module);
    add_module(m_pager);
    add_module(m_selectcomplete);
    add_module(m_textlist);

    key_tester* old_tester = desc.input->set_key_tester(this);
    assert(!old_tester);
}

//------------------------------------------------------------------------------
line_editor_impl::~line_editor_impl()
{
    // Ensure cleanup if/when clatch's REQUIRE throws.
    if (check_flag(flag_editing))
        end_line();

    m_desc.input->set_key_tester(nullptr);
}

//------------------------------------------------------------------------------
void line_editor_impl::initialise()
{
    if (check_flag(flag_init))
        return;

    struct : public editor_module::binder {
        virtual int32 get_group(const char* name) const override
        {
            return binder->get_group(name);
        }

        virtual int32 create_group(const char* name) override
        {
            return binder->create_group(name);
        }

        virtual bool bind(uint32 group, const char* chord, uint8 key, bool has_params=false) override
        {
            return binder->bind(group, chord, *module, key, has_params);
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

#ifdef DEBUG
    m_signaled = false;
#endif

    m_bind_resolver.reset();
    m_command_offset = 0;
    m_prev_key.reset();

    set_active_line_editor(this, m_desc.callbacks);

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    const int32 began = m_desc.input->begin();
    assert(began == 1);
    m_desc.output->begin();
    m_buffer.begin_line();

    m_prev_generate.clear();
    m_prev_plain = false;
    m_prev_cursor = 0;
    m_prev_classify.clear();
    m_prev_command_word.clear();
    m_prev_command_word_offset = -1;
    m_prev_command_word_quoted = false;

    m_words.clear();
    m_command_line_states.clear();
    m_classify_words.clear();

    m_override_needle = nullptr;
    m_override_words.clear();
    m_override_command_line_states.clear();

    rl_before_display_function = ::before_display_readline;

    editor_module::context context = get_context();
    for (auto module : m_modules)
        module->on_begin_line(context);

#ifdef DEBUG
    m_in_matches_ready = false;
#endif
}

//------------------------------------------------------------------------------
void line_editor_impl::end_line()
{
    assert(check_flag(flag_editing));

    for (auto i = m_modules.rbegin(), n = m_modules.rend(); i != n; ++i)
        i->on_end_line();

    rl_before_display_function = nullptr;

    m_buffer.end_line();
    m_desc.output->end();
    const int32 began = m_desc.input->end();
    assert(!began);

    m_words.clear();
    m_command_line_states.clear();
    m_classify_words.clear();

    set_active_line_editor(nullptr, nullptr);

    clear_flag(flag_editing);

    assert(!m_in_matches_ready);
    assert(!m_buffer.has_override());
}

//------------------------------------------------------------------------------
bool line_editor_impl::add_module(editor_module& module)
{
    // Don't add duplicates.  This mainly matters for the test program.
    for (const auto& m : m_modules)
    {
        if (m == &module)
            return true;
    }

    // Add the module at the back of the list.
    editor_module** slot = m_modules.push_back();
    return (slot != nullptr) ? *slot = &module, true : false;
}

//------------------------------------------------------------------------------
void line_editor_impl::set_generator(match_generator& generator)
{
    m_generator = &generator;
    m_regen_matches.set_generator(&generator);
    m_matches.set_generator(&generator);
}

//------------------------------------------------------------------------------
void line_editor_impl::set_hinter(hinter& hinter)
{
    m_hinter = &hinter;
}

//------------------------------------------------------------------------------
void line_editor_impl::set_classifier(word_classifier& classifier)
{
    m_classifier = &classifier;
}

//------------------------------------------------------------------------------
void line_editor_impl::set_input_idle(input_idle* idle)
{
    m_idle = idle;
}

//------------------------------------------------------------------------------
void line_editor_impl::set_prompt(const char* prompt, const char* rprompt, bool redisplay, bool transient)
{
    m_desc.prompt = prompt;
    m_module.set_prompt(prompt, rprompt, redisplay, transient);
}

//------------------------------------------------------------------------------
bool line_editor_impl::get_line(str_base& out)
{
    if (check_flag(flag_editing))
        end_line();

    if (check_flag(flag_eof))
    {
        out.clear();
        return false;
    }

    m_module.next_line(out);
    return true;
}

//------------------------------------------------------------------------------
bool line_editor_impl::edit(str_base& out, bool edit)
{
    if (!check_flag(flag_editing))
        m_insert_on_begin = out.c_str();

    if (edit)
    {
        // Update first so the init state goes through.
        while (update())
        {
            if (!m_module.is_input_pending())
                m_desc.input->select(m_idle);
        }
    }
    else
    {
        update();
        rl_newline(0, 0);
    }

    return get_line(out);
}

//------------------------------------------------------------------------------
void line_editor_impl::override_line(const char* line, const char* needle, int32 point)
{
    assert(!line || !m_buffer.has_override());
    assert(!line || point >= 0);
    assert(!line || point <= strlen(line));

    m_buffer.override(line, point);
    m_override_needle = line ? needle : nullptr;

    m_override_words.clear();
    m_override_command_line_states.clear();
    if (line)
    {
        collect_words(m_override_words, &m_matches, collect_words_mode::stop_at_cursor, m_override_command_line_states);
        set_flag(flag_generate);
    }
}

//------------------------------------------------------------------------------
#ifdef DEBUG
bool line_editor_impl::is_line_overridden()
{
    return m_buffer.has_override();
}
#endif

//------------------------------------------------------------------------------
bool line_editor_impl::update()
{
    if (!check_flag(flag_init))
        initialise();

    if (!check_flag(flag_editing))
    {
        begin_line();
        if (m_insert_on_begin)
        {
            m_buffer.insert(m_insert_on_begin);
            m_buffer.draw();
            m_insert_on_begin = nullptr;
        }
        update_internal();
        return true;
    }

    update_input();

    maybe_handle_signal();

    if (!check_flag(flag_editing))
        return false;

    update_internal();
    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::reset_generate_matches()
{
    set_flag(flag_generate);
    set_flag(flag_select);
    m_prev_key.reset();
    m_prev_generate.clear();
}

//------------------------------------------------------------------------------
void line_editor_impl::reselect_matches()
{
    set_flag(flag_select);
}

//------------------------------------------------------------------------------
void line_editor_impl::force_update_internal(bool restrict)
{
    update_internal();
    if (restrict) set_flag(flag_restrict);
    set_flag(flag_select);
}

//------------------------------------------------------------------------------
bool line_editor_impl::notify_matches_ready(int32 generation_id, matches* matches)
{
#ifdef DEBUG
    assert(!m_in_matches_ready);
    rollback<bool> rb(m_in_matches_ready, true);
#endif

    // The generation matches, then use the newly generated matches.
    if (matches && generation_id == m_generation_id)
    {
        assert(&m_matches != matches);
        m_matches.transfer(static_cast<matches_impl&>(*matches));
        m_matches.done_building();
        clear_flag(flag_generate);
    }
    else
    {
        // There's a newer generation id, so force a new suggestion, since the
        // newer generation id's autosuggest will have been canceled due to the
        // match generator coroutine that was already running, and which has
        // just now signaled its completion.
        clear_suggestion();
    }

    // Trigger generating suggestion again.
    {
        // Don't generate matches again unless the input line has changed;
        // notify_matches_ready() is called when matches become available, but
        // try_suggest() invokes match generation when needed.  Volatile
        // matches can create an infinite cycle of notifying and regenerating.
        ignore_volatile_matches ignore(m_matches);
        try_suggest();
    }

    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::update_matches()
{
    if (m_matches.is_volatile())
        reset_generate_matches();

    // Get flag states because we're about to clear them.
    bool generate = check_flag(flag_generate);
    bool restrict = check_flag(flag_restrict);
    bool select = (generate ||
                   restrict ||
                   check_flag(flag_select) ||
                   m_matches.get_completion_type() != rl_completion_type);

    // Clear flag states before running generators, so that generators can use
    // reset_generate_matches().
    clear_flag(flag_generate);
    clear_flag(flag_restrict);
    clear_flag(flag_select);

    if (generate)
    {
        const auto linestates = get_linestates();
        match_pipeline pipeline(m_matches);
        pipeline.reset();
        pipeline.generate(linestates, m_generator);
    }

    if (restrict && !m_buffer.has_override())
    {
        match_pipeline pipeline(m_matches);

        // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.  Stripping
        // quotes may seem surprising, but it's what CMD does and it works well.
        str_moveable tmp;
        concat_strip_quotes(tmp, m_needle.c_str(), m_needle.length());

        bool just_tilde = false;
        if (rl_complete_with_tilde_expansion && tmp.c_str()[0] == '~')
        {
            just_tilde = !tmp.c_str()[1];
            if (!path::tilde_expand(tmp))
                just_tilde = false;
            else if (just_tilde)
                path::maybe_strip_last_separator(tmp);
        }

        m_needle = tmp.c_str();
        if (!is_literal_wild() && !just_tilde)
            m_needle.concat("*", 1);
        pipeline.restrict(m_needle);
    }

    if (select)
    {
        const char* needle = m_buffer.has_override() ? m_override_needle : m_needle.c_str();
        match_pipeline pipeline(m_matches);
        pipeline.select(needle);
        pipeline.sort();
    }

    // Tell all the modules that the matches changed.
    if (generate || restrict || select)
    {
        const char* needle = m_buffer.has_override() ? m_override_needle : m_needle.c_str();
        line_state line = get_linestate();
        editor_module::context context = get_context();
        for (auto module : m_modules)
            module->on_matches_changed(context, line, needle);
    }
}

//------------------------------------------------------------------------------
void line_editor_impl::dispatch(int32 bind_group)
{
    assert(check_flag(flag_init));
    assert(check_flag(flag_editing));

    // Claim any pending binding, otherwise we'll try to dispatch it again.

    if (m_pending_binding)
        m_pending_binding->claim();

    // Handle one input.

    const int32 prev_bind_group = m_bind_resolver.get_group();
    m_bind_resolver.set_group(bind_group);

    const bool was_signaled = clink_is_signaled();

    m_dispatching++;

    do
    {
        m_desc.input->select();
        m_invalid_dispatch = false;
    }
    while (!update_input() || m_invalid_dispatch);

    m_dispatching--;

    assert(check_flag(flag_editing));

    m_bind_resolver.set_group(prev_bind_group);
}

//------------------------------------------------------------------------------
bool line_editor_impl::available(uint32 timeout)
{
    assert(check_flag(flag_init));
    return m_desc.input->available(timeout);
}

//------------------------------------------------------------------------------
uint8 line_editor_impl::peek()
{
    assert(check_flag(flag_init));
    const int32 c = m_desc.input->peek();
    assert(c < 0xf8);
    return (c < 0) ? 0 : uint8(c);
}

//------------------------------------------------------------------------------
bool line_editor_impl::is_bound(const char* seq, int32 len)
{
    // Check if clink has a binding; these override Readline.
    int32 bound = m_bind_resolver.is_bound(seq, len);
    if (bound != 0)
        return (bound > 0);

    // Check if Readline has a binding.
    return m_module.is_bound(seq, len);
}

//------------------------------------------------------------------------------
bool line_editor_impl::accepts_mouse_input(mouse_input_type type)
{
    if (m_selectcomplete.is_active())
        return m_selectcomplete.accepts_mouse_input(type);
    if (m_textlist.is_active())
        return m_textlist.accepts_mouse_input(type);
    if (m_bind_resolver.get_group() == m_binder.get_group())
        return m_module.accepts_mouse_input(type);
    return false;
}

//------------------------------------------------------------------------------
bool line_editor_impl::translate(const char* seq, int32 len, str_base& out)
{
    return m_module.translate(seq, len, out);
}

//------------------------------------------------------------------------------
// Returns false when a chord is in progress, otherwise returns true.  This is
// to help dispatch() be able to dispatch an entire chord.
bool line_editor_impl::update_input()
{
    if (maybe_handle_signal())
        return true;

    if (!m_module.is_input_pending())
    {
        int32 key = m_desc.input->read();

        if (key == terminal_in::input_terminal_resize)
        {
            int32 columns = m_desc.output->get_columns();
            int32 rows = m_desc.output->get_rows();
            editor_module::context context = get_context();
            for (auto* module : m_modules)
                module->on_terminal_resize(columns, rows, context);
        }

        if (key == terminal_in::input_abort)
        {
            if (!m_dispatching)
            {
                m_buffer.reset();
                end_line();
            }
            return true;
        }

        if (key == terminal_in::input_exit)
        {
            if (!m_dispatching)
            {
                m_buffer.reset();
                m_buffer.insert("exit 0");
#ifdef DEBUG
                ignore_column_in_uninit_display_readline();
#endif
                end_line();
            }
            return true;
        }

        if (key < 0)
            return true;

        // `quoted-insert` should always behave as though the key resolved a
        // binding, to ensure that Readline gets to handle the key (even Esc).
        if (!m_bind_resolver.step(key) &&
            !rl_is_insert_next_callback_pending() &&
            !rl_vi_insert_mode_esc_special_case(key))
            return false;
    }

    struct result_impl : public editor_module::result
    {
        enum
        {
            flag_pass       = 1 << 0,
            flag_invalid    = 1 << 1,
            flag_done       = 1 << 2,
            flag_eof        = 1 << 3,
            flag_redraw     = 1 << 4,
        };

        virtual void    pass() override                           { flags |= flag_pass; }
        virtual void    loop() override                           { flags |= flag_invalid; }
        virtual void    done(bool eof) override                   { flags |= flag_done|(eof ? flag_eof : 0); }
        virtual void    redraw() override                         { flags |= flag_redraw; }
        virtual int32   set_bind_group(int32 id) override         { int32 t = group; group = id; return t; }
        unsigned short  group;  //        <! MSVC bugs; see connect
        uint8           flags;  // = 0;   <! issues about C2905
    };

    while (auto binding = m_bind_resolver.next())
    {
        // Binding found, dispatch it off to the module.
        result_impl result;
        result.flags = 0;
        result.group = m_bind_resolver.get_group();

        str<16> chord;
        editor_module* module = binding.get_module();
        uint8 id = binding.get_id();
        binding.get_chord(chord);

        {
            rollback<bind_resolver::binding*> _(m_pending_binding, &binding);

            editor_module::context context = get_context();
            editor_module::input input = { chord.c_str(), chord.length(), id, m_bind_resolver.more_than(chord.length()), binding.get_params() };
            module->on_input(input, result, context);

            if (clink_is_signaled())
                return true;
        }

        m_bind_resolver.set_group(result.group);

        // Process what result_impl has collected.

        if (binding) // May have been claimed already by dispatch() inside on_input().
        {
            if (result.flags & result_impl::flag_pass)
            {
                // win_terminal_in avoids producing input that won't be handled.
                // But it can't predict when result.pass() might be used, so the
                // onus is on the pass() caller to make sure passing the binding
                // upstream won't leave it unhandled.  If it's unhandled, then
                // the key sequence gets split at the point of mismatch, and the
                // rest gets interpreted as a separate key sequence.
                //
                // For example, mouse input can be especially susceptible.
                continue;
            }
            binding.claim();
        }

        if (m_dispatching)
        {
            if (result.flags & result_impl::flag_invalid)
                m_invalid_dispatch = true;
        }
        else
        {
            before_display_readline();

            if (result.flags & result_impl::flag_done)
            {
                end_line();

                if (result.flags & result_impl::flag_eof)
                    set_flag(flag_eof);
            }

            if (!check_flag(flag_editing))
                return true;
        }

        if (result.flags & result_impl::flag_redraw)
            m_buffer.redraw();
    }

    m_buffer.draw();
    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::collect_words()
{
    m_module.clear_need_collect_words();
    m_command_offset = collect_words(m_words, &m_matches, collect_words_mode::stop_at_cursor, m_command_line_states);
}

//------------------------------------------------------------------------------
command_line_states line_editor_impl::collect_command_line_states()
{
    command_line_states command_line_states;
    collect_words(m_classify_words, nullptr, collect_words_mode::whole_command, command_line_states);
    return command_line_states;
}

//------------------------------------------------------------------------------
uint32 line_editor_impl::collect_words(words& words, matches_impl* matches, collect_words_mode mode, command_line_states& command_line_states)
{
    std::vector<command> commands;
    uint32 command_offset = m_collector.collect_words(m_buffer, words, mode, &commands);
    command_line_states.set(m_buffer, words, commands);

#ifdef DEBUG
    const int32 dbg_row = dbg_get_env_int("DEBUG_COLLECTWORDS");
    str<> tmp1;
    str<> tmp2;
    if (dbg_row > 0)
    {
        if (words.size() > 0)
        {
            bool command = true;
            int32 i_word = 1;
            tmp1.format("\x1b[s\x1b[%dHcollected words:        ", dbg_row);
            m_printer.print(tmp1.c_str(), tmp1.length());
            for (auto const& w : words)
            {
                tmp1.format("\x1b[90m%u\x1b[m", i_word);
                m_printer.print(tmp1.c_str(), tmp1.length());

                const char* q = w.quoted ? "\"" : "";
                if (w.command_word)
                    command = true;
                if (w.is_redir_arg)
                    m_printer.print(">");
                if (w.command_word)
                    m_printer.print("!");
                const char* color = "37";
                if (command && !w.is_redir_arg)
                {
                    command = false;
                    color = "4;32";
                }
                if (w.length)
                    tmp1.format("%s\x1b[0;%s;7m%.*s\x1b[m%s ", q, color, w.length, m_buffer.get_buffer() + w.offset, q);
                else
                    tmp1.format("\x1b[0;37;7m \x1b[m ");
                m_printer.print(tmp1.c_str(), tmp1.length());
                i_word++;
            }
            m_printer.print("\x1b[K\x1b[u");
        }
        else
        {
            tmp1.format("\x1b[s\x1b[%dH\x1b[mno words collected\x1b[K\x1b[u", dbg_row);
            m_printer.print(tmp1.c_str(), tmp1.length());
            tmp2.format("\x1b[s\x1b[%dH\x1b[m\x1b[K\x1b[u", dbg_row + 1);
            m_printer.print(tmp2.c_str(), tmp2.length());
        }
    }
#endif

    // The last word can be split by the match generators, to influence word
    // breaks. This is a little clunky but works well enough.  And because
    // "nowordbreakchars" allows unbreaking adjacent words and internal CMD
    // commands use different nowordbreakchars than other commands, it's now
    // necessary to call get_word_break_info() even when the last word is
    // empty.
    if (mode == collect_words_mode::stop_at_cursor)
    {
        word_break_info break_info;
        if (m_generator)
            m_generator->get_word_break_info(command_line_states.get_linestate(m_buffer), break_info);
        const uint32 end_word_offset = command_line_states.break_end_word(break_info.truncate, break_info.keep);

#ifdef DEBUG
        if (dbg_row > 0)
        {
            bool command = true;
            int32 i_word = 1;
            tmp2.format("\x1b[s\x1b[%dHafter word break info:  ", dbg_row + 1);
            m_printer.print(tmp2.c_str(), tmp2.length());
            auto const& after_break_words = command_line_states.get_linestate(m_buffer).get_words();
            for (auto const& w : after_break_words)
            {
                tmp1.format("\x1b[90m%u\x1b[m", i_word);
                m_printer.print(tmp1.c_str(), tmp1.length());

                const char* q = w.quoted ? "\"" : "";
                if (w.command_word)
                    command = true;
                if (w.is_redir_arg)
                    m_printer.print(">");
                if (w.command_word)
                    m_printer.print("!");
                const char* color = "37;7";
                if (command && !w.is_redir_arg)
                {
                    command = false;
                    color = "4;32;7";
                }
                if (i_word == after_break_words.size())
                    color = "35;7";
                tmp2.format("%s\x1b[0;%sm%.*s\x1b[m%s ", q, color, w.length, m_buffer.get_buffer() + w.offset, q);
                m_printer.print(tmp2.c_str(), tmp2.length());
                i_word++;
            }
            m_printer.print("\x1b[K\x1b[u");
        }
#endif

        // Need to coordinate with Readline when we redefine word breaks.
        assert(matches);
        matches->set_word_break_position(end_word_offset);
    }

#ifdef DEBUG
    if (dbg_row > 0)
    {
        if (tmp2.empty())
        {
            tmp2.format("\x1b[s\x1b[%dH\x1b[m\x1b[K\x1b[u", dbg_row + 1);
            m_printer.print(tmp2.c_str(), tmp2.length());
        }
    }
#endif

    words.clear();
    for (const auto& w : command_line_states.get_linestate(m_buffer).get_words())
        words.emplace_back(w);

    return command_offset;
}

//------------------------------------------------------------------------------
void line_editor_impl::before_display_readline()
{
    // Temporarily strip off suggestions.
    rollback<int32> rb_end(rl_end);
    if (g_suggestion_offset >= 0)
        rl_end = g_suggestion_offset;

    // Skip parsing if the line buffer hasn't changed.
    const bool plain = !!RL_ISSTATE(RL_STATE_NSEARCH|RL_STATE_READSTR);
    const bool plain_changed = (m_prev_plain != plain);
    const bool buffer_changed = (plain_changed || !m_prev_classify.equals(m_buffer.get_buffer(), m_buffer.get_length()));
    const bool skip_classifier = (!m_classifier || !buffer_changed);
    const bool skip_hinter = (!m_hinter || !s_comment_row_show_hints.get() || (!buffer_changed && m_prev_cursor == m_buffer.get_cursor()));
    if (skip_classifier && skip_hinter)
        return;

    bool calced_history_expansions = false;
    history_expansion* list = nullptr;
    command_line_states command_line_states = ((!skip_classifier && !plain) || (!skip_hinter)) ? collect_command_line_states() : ::command_line_states();

    if (!skip_classifier)
    {
        // Hang on to the old classifications so it's possible to detect changes.
        word_classifications old_classifications(std::move(m_classifications));
        m_classifications.init(m_buffer.get_length(), &old_classifications);

        if (plain)
        {
            m_classifications.apply_face(0, m_buffer.get_length(), FACE_NORMAL);
            m_classifications.finish(m_module.is_showing_argmatchers());
        }
        else
        {
            // Use the full line; don't stop at the cursor.
            m_classifier->classify(command_line_states.get_linestates(m_buffer), m_classifications);
            if (g_history_autoexpand.get() &&
                g_expand_mode.get() &&
                (g_history_show_preview.get() ||
                !is_null_or_empty(g_color_histexpand.get())))
            {
                calc_history_expansions(m_buffer, list);
                classify_history_expansions(list, m_classifications);
                set_history_expansions(list);
                calced_history_expansions = true;
            }
            m_classifications.finish(m_module.is_showing_argmatchers());
        }

#ifdef DEBUG
        const int32 dbgrow = dbg_get_env_int("DEBUG_CLASSIFY");
        if (dbgrow)
        {
            str<> c;
            str<> f;
            str<> tmp;
            static const char *const word_class_name[] = {"other", "unrecognized", "executable", "command", "doskey", "arg", "flag", "none"};
            static_assert(sizeof_array(word_class_name) == int32(word_class::max), "word_class flag count mismatch");
            word_class wc;
            for (uint32 i = 0; i < m_classifications.size(); ++i)
            {
                if (m_classifications.get_word_class(i, wc))
                {
                    tmp.format(" \x1b[90m%d\x1b[m\x1b[7m%c\x1b[m", i + 1, word_class_name[int32(wc)][0]);
                    c.concat(tmp.c_str(), tmp.length());
                }
            }
            for (uint32 i = 0; i < m_classifications.length(); ++i)
            {
                char face = m_classifications.get_face(i);
                f.concat("\x1b[7m");
                if (face >= 0x20 && face <= 0x7f)
                    f.concat(&face, 1);
                else
                {
                    tmp.format("%X", uint8(face));
                    f.concat(tmp.c_str(), tmp.length());
                }
                f.concat("\x1b[m");
            }
            if (dbgrow < 0)
            {
                printf("CLASSIFICATIONS %s  FACES \"%s\"\n", c.c_str(), f.c_str());
            }
            else
            {
                printf("\x1b[s");
                printf("\x1b[%uHCLASSIFICATIONS %s\x1b[m\x1b[K", dbgrow, c.c_str());
                printf("\x1b[%uHFACES            %s\x1b[m\x1b[K", dbgrow + 1, f.c_str());
                printf("\x1b[u");
            }
        }
#endif

        if (!old_classifications.equals(m_classifications))
            m_buffer.set_need_draw();
    }

    if (!skip_hinter)
    {
        // Hang on to the old hint so it's possible to detect changes.
        const input_hint old_hint(std::move(m_input_hint));

        if (!calced_history_expansions && g_history_autoexpand.get() && g_expand_mode.get() && g_history_show_preview.get())
        {
            history_expansion* list = nullptr;
            calc_history_expansions(m_buffer, list);
            set_history_expansions(list);
        }

#ifdef DEBUG
        const double clock = os::clock();
#endif

        m_hinter->get_hint(command_line_states.get_linestate(m_buffer), m_input_hint);

#ifdef DEBUG
        const int32 dbgrow = dbg_get_env_int("DEBUG_HINTER");
        if (dbgrow)
        {
            static uint32 s_executed = 0;
            const double elapsed = os::clock() - clock;
            ++s_executed;

            str<> format;
            if (m_input_hint.empty())
                format.format("%u  HINT (none), elapsed %0.04f sec", s_executed, elapsed);
            else
                format.format("%u  HINT \"%%s\" pos %u, elapsed %0.04f sec", s_executed, m_input_hint.pos(), elapsed);

            str<> expanded;
            const int32 limit = (m_input_hint.empty() || dbgrow < 0) ? 9999 : _rl_screenwidth - 1 - cell_count(format.c_str());
            if (limit > 0)
            {
                ellipsify(m_input_hint.c_str(), max(1, limit), expanded, true);
                if (dbgrow < 0)
                {
                    printf("%s\n", format.c_str());
                }
                else
                {
                    printf("\x1b[s\x1b[%uH", dbgrow);
                    printf(format.c_str(), expanded.c_str());
                    printf("\x1b[m\x1b[K\x1b[u");
                }
            }
        }
#endif

        if (!old_hint.equals(m_input_hint))
            m_buffer.set_need_draw();
    }
    else
    {
        m_input_hint.clear();
    }

    m_prev_plain = plain;
    m_prev_cursor = m_buffer.get_cursor();
    m_prev_classify.set(m_buffer.get_buffer(), m_buffer.get_length());
}

//------------------------------------------------------------------------------
void line_editor_impl::maybe_send_oncommand_event()
{
    if (!m_desc.callbacks)
        return;

    line_state line = get_linestate();
    if (line.get_word_count() <= 1)
        return;

    const word* p = nullptr;
    for (size_t i = 0; i < line.get_words().size(); ++i)
    {
        const word& tmp = line.get_words()[i];
        if (!tmp.is_redir_arg)
        {
            p = &tmp;
            break;
        }
    }
    if (!p || !p->length)
        return;

    const word& info = *p;
    if (m_prev_command_word_quoted == info.quoted &&
        m_prev_command_word_offset == info.offset &&
        m_prev_command_word.length() == info.length &&
        _strnicmp(m_prev_command_word.c_str(), m_buffer.get_buffer() + info.offset, info.length) == 0)
        return;

    str<> first_word;
    uint32 offset = info.offset;
    bool quoted = info.quoted;
    first_word.concat(line.get_line() + info.offset, info.length);

    if (m_desc.callbacks->has_event_handler("oncommand"))
    {
        doskey_alias resolved;
        doskey doskey("cmd.exe");
        doskey.resolve(line.get_line(), resolved);

        str<32> command;
        str_moveable tmp;
        const char* lookup = first_word.c_str();
        if (resolved && resolved.next(tmp))
        {
            str_tokeniser tokens(tmp.c_str(), " \t");
            tokens.add_quote_pair("\"");

            const char *start;
            int32 length;
            str_token token = tokens.next(start, length);
            if (token)
            {
                command.concat(start, length);
                quoted = (start > line.get_line() && start[-1] == '"');
                lookup = command.c_str();
            }
        }

        bool ready;
        str<> file;
        const recognition recognized = recognize_command(line.get_line(), lookup, quoted, ready, &file);
        if (!ready || recognized == recognition::unknown)
            return;

        m_desc.callbacks->send_oncommand_event(line, lookup, quoted, recognized, file.c_str());
    }

    m_prev_command_word = first_word.c_str();
    m_prev_command_word_offset = offset;
    m_prev_command_word_quoted = quoted;
}

//------------------------------------------------------------------------------
void line_editor_impl::reclassify(reclassify_reason why)
{
    // Test check_recognizer_refresh() first, to ensure its side effects occur
    // when necessary.
    const bool refresh = check_recognizer_refresh();

    if (refresh)
    {
        why = reclassify_reason::force;
        maybe_send_oncommand_event();
    }

    if (refresh || why == reclassify_reason::force || why == reclassify_reason::hinter)
    {
        if (why == reclassify_reason::force)
        {
            m_prev_plain = false;
            m_prev_cursor = 0;
            m_prev_classify.clear();
        }
        m_buffer.set_need_draw();
        m_buffer.draw();
    }
}

//------------------------------------------------------------------------------
line_state line_editor_impl::get_linestate() const
{
    if (m_buffer.has_override())
        return m_override_command_line_states.get_linestate(m_buffer);

    return m_command_line_states.get_linestate(m_buffer);
}

//------------------------------------------------------------------------------
line_states line_editor_impl::get_linestates() const
{
    if (m_buffer.has_override())
        return m_override_command_line_states.get_linestates(m_buffer);

    return m_command_line_states.get_linestates(m_buffer);
}

//------------------------------------------------------------------------------
editor_module::context line_editor_impl::get_context() const
{
    auto& pter = const_cast<printer&>(m_printer);
    auto& pger = const_cast<pager&>(static_cast<const pager&>(m_pager));
    auto& buffer = const_cast<rl_buffer&>(m_buffer);
    return { m_desc.prompt, m_desc.rprompt, pter, pger, buffer, m_matches, m_classifications, m_input_hint };
}

//------------------------------------------------------------------------------
void line_editor_impl::set_flag(uint8 flag)
{
    m_flags |= flag;

    if (flag & flag_generate)
        m_generation_id = max<int32>(m_generation_id + 1, 1);
}

//------------------------------------------------------------------------------
void line_editor_impl::clear_flag(uint8 flag)
{
    m_flags &= ~flag;
}

//------------------------------------------------------------------------------
bool line_editor_impl::check_flag(uint8 flag) const
{
    return ((m_flags & flag) != 0);
}

//------------------------------------------------------------------------------
bool line_editor_impl::maybe_handle_signal()
{
    if (m_dispatching)
    {
        const int32 sig = clink_is_signaled();
        return !!sig;
    }
    else
    {
        const int32 sig = clink_maybe_handle_signal();
        if (!sig)
            return false;

#ifdef DEBUG
        assert(!m_signaled);
        m_signaled = true;
#endif

        for (auto* module : m_modules)
            module->on_signal(sig);
        m_buffer.reset();

        end_line();
        return true;
    }
}

//------------------------------------------------------------------------------
bool line_editor_impl::is_key_same(const key_t& prev_key, const char* prev_line, int32 prev_length,
                                   const key_t& next_key, const char* next_line, int32 next_length,
                                   bool compare_cursor)
{
    // If the word indices are different, the keys are different.  Argmatchers
    // and generators may treat the same word differently based on its position.
    if (prev_key.word_index != next_key.word_index)
        return false;

    // If the key lengths are different, the keys are different.  Their content
    // can't be the same if their lengths aren't the same.
    if (prev_key.word_length != next_key.word_length)
        return false;

    // If the key offsets are different, the keys are different.  Different
    // offsets means the preceding input line content is different, which means
    // argmatchers and generators may need to parse the input line differently.
    if (prev_key.word_offset != next_key.word_offset)
        return false;

    // If the cursor positions are different, the keys are different.  Again,
    // argmatchers and generators may need to parse the input line differently.
    if (compare_cursor && prev_key.cursor_pos != next_key.cursor_pos)
        return false;

    // If the key contents are different, the keys are different.  This can
    // occur in various situations.  For example when `menu-complete` replaces
    // `dir\sub1\` with `dir\sub2\`.
    str_iter prev(prev_line + prev_key.word_offset, min(int32(prev_key.word_length), max(0, int32(prev_length - prev_key.word_offset))));
    str_iter next(next_line + next_key.word_offset, min(int32(next_key.word_length), max(0, int32(next_length - next_key.word_offset))));
    for (int32 i = prev.length(); i--;)
        if (prev.get_pointer()[i] != next.get_pointer()[i])
            return false;

    // The keys are the same.
#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_KEYSAME"))
    {
        printf("SAME: prev '%.*s' %d,%d,%d,%d vs next '%.*s' %d,%d,%d,%d\n",
               prev.length(), prev.get_pointer(), prev_key.word_index, prev_key.word_offset, prev_key.word_length, compare_cursor ? prev_key.cursor_pos : 0,
               next.length(), next.get_pointer(), next_key.word_index, next_key.word_offset, next_key.word_length, compare_cursor ? next_key.cursor_pos : 0);
    }
#endif
    return true;
}

//------------------------------------------------------------------------------
matches* line_editor_impl::get_mutable_matches(bool nosort)
{
    collect_words();

    assert(m_words.size() > 0);
    const word& end_word = m_words.back();
    const key_t next_key = { (uint32)m_words.size() - 1, end_word.offset, end_word.length, m_buffer.get_cursor() };

    m_prev_key = next_key;
    m_prev_generate.set(m_buffer.get_buffer(), end_word.offset + end_word.length);

    match_pipeline pipeline(m_matches);
    pipeline.reset();
    if (nosort)
        pipeline.set_no_sort();

    m_matches.set_word_break_position(end_word.offset);
    m_matches.set_regen_blocked();

    clear_flag(flag_generate);
    set_flag(flag_select);

    return &m_matches;
}

//------------------------------------------------------------------------------
void line_editor_impl::update_internal()
{
    // This is responsible for updating the matches for the word under the
    // cursor.  It tries to call match generators only once for the current
    // word, and then repeatedly filter the results as the word is edited.

    // Collect words.  To keep things simple for match generators, only text to
    // to the cursor word is relevant, so that the "end word" is the word at the
    // cursor.  To separate the generate phase and select+sort phase, the end
    // word is always returned as empty.
    collect_words();

    assert(m_words.size() > 0);
    const word& end_word = m_words.back();

    const key_t next_key = { (uint32)m_words.size() - 1, end_word.offset, end_word.length, m_buffer.get_cursor() };
    const key_t prev_key = m_prev_key;

    // Should we generate new matches?  Matches are generated for the end word
    // position.  If the end word hasn't changed, then don't generate matches.
    // Since the end word is empty, don't compare the cursor position, so the
    // matches are only collected once for the word position.
    int32 update_prev_generate = -1;
    if (!is_key_same(prev_key, m_prev_generate.get(), m_prev_generate.length(),
                     next_key, m_buffer.get_buffer(), m_buffer.get_length(),
                     false/*compare_cursor*/))
    {
        line_state line = get_linestate();
        str_iter end_word = line.get_end_word();
        int32 len = int32(end_word.get_pointer() + end_word.length() - line.get_line());
        if (!m_prev_generate.equals(line.get_line(), len))
        {
            // While clink-select-complete is active, generating must be blocked
            // otherwise it will constantly generate in response to every input.
            if (!m_selectcomplete.is_active())
            {
                match_pipeline pipeline(m_matches);
                pipeline.reset();

                // Defer generating until update_matches().  Must set word break
                // position in the meantime because adjust_completion_word()
                // gets called before the deferred generate().
                set_flag(flag_generate);
                m_matches.set_word_break_position(line.get_end_word_offset());
            }
            update_prev_generate = len;
        }
    }

    // Should we select and sort matches?  Matches are filtered and sorted for
    // the portion of the end word up to the cursor.
    if (!is_key_same(prev_key, m_prev_generate.get(), m_prev_generate.length(),
                     next_key, m_buffer.get_buffer(), m_buffer.get_length(),
                     true/*compare_cursor*/))
    {
        int32 needle_start = end_word.offset;
        const char* buf_ptr = m_buffer.get_buffer();

        // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.  Stripping
        // quotes may seem surprising, but it's what CMD does and it works well.
        m_needle.clear();
        concat_strip_quotes(m_needle, buf_ptr + needle_start, next_key.cursor_pos - needle_start);

        if (!m_needle.empty() && end_word.quoted)
        {
            int32 i = m_needle.length();
            if (m_needle[i - 1] == get_closing_quote(m_desc.get_quote_pair()))
                m_needle.truncate(i - 1);
        }

        set_flag(flag_select);          // Defer selecting until update_matches().

        m_prev_key = next_key;
    }

    // Send oncommand event when command word changes.
    maybe_send_oncommand_event();

    // Should we collect suggestions?
    try_suggest();

    // Must defer updating m_prev_generate since the old value is still needed
    // for deciding whether to sort/select, after deciding whether to generate.
    if (update_prev_generate >= 0)
        m_prev_generate.set(m_buffer.get_buffer(), update_prev_generate);

    if (is_endword_tilde(get_linestate()))
        reset_generate_matches();
}

//------------------------------------------------------------------------------
void line_editor_impl::try_suggest()
{
    if (!g_autosuggest_enable.get())
        return;

    const line_states& lines = m_command_line_states.get_linestates(m_buffer);
    line_state line = lines.back();
    if (host_can_suggest(line))
    {
        matches_impl* matches = nullptr;
        matches_impl* empty_matches = nullptr;

        if (m_words.size())
        {
            const word& word = m_words.back();
            if (word.offset < m_buffer.get_length() && m_buffer.get_length() - word.offset >= 2)
            {
                // Use an empty set of matches when a UNC path is detected.
                // Or when a remote drive is detected (or unknown or invalid).
                // Removable drives are accepted because typically these are
                // thumb drives these days, which are fast.
                const char* end_word = m_buffer.get_buffer() + word.offset;
                bool no_matches = path::is_unc(end_word);
                if (!no_matches)
                {
                    str<> drive;
                    if (!path::get_drive(end_word, drive))
                    {
                        str<> cwd;
                        os::get_current_dir(cwd);
                        path::get_drive(cwd.c_str(), drive);
                    }
                    if (!drive.empty())
                    {
                        path::append(drive, ""); // Because get_drive_type() requires a trailing path separator.
                        no_matches = (os::get_drive_type(drive.c_str()) < os::drive_type_removable);
                    }
                }
                if (no_matches)
                    matches = empty_matches = new matches_impl;
            }
        }

        // Never generate matches here; let it be deferred and happen on demand
        // in a coroutine.
        if (!empty_matches && (!g_autosuggest_async.get() ||
                               (!check_flag(flag_generate) && !m_matches.is_volatile())))
        {
            // Suggestions must use the TAB completion type, because they
            // cannot work with wildcards or substrings.
            rollback<int32> rb_completion_type(rl_completion_type, TAB);

            update_matches();
            matches = &m_matches;
        }

        host_suggest(lines, matches, m_generation_id);

        delete empty_matches;
    }
}

//------------------------------------------------------------------------------
bool line_editor_impl::call_lua_rl_global_function(const char* func_name)
{
    line_state line = get_linestate();
    return host_call_lua_rl_global_function(func_name, &line);
}

//------------------------------------------------------------------------------
uint32 line_editor_impl::collect_words(const line_buffer& buffer, std::vector<word>& words, collect_words_mode mode) const
{
    return m_collector.collect_words(buffer, words, mode, nullptr);
}

//------------------------------------------------------------------------------
DWORD line_editor_impl::get_input_hint_timeout() const
{
    return m_input_hint.get_timeout();
}

//------------------------------------------------------------------------------
void line_editor_impl::clear_input_hint_timeout()
{
    m_input_hint.clear_timeout();
}

//------------------------------------------------------------------------------
const input_hint* line_editor_impl::get_input_hint() const
{
    return &m_input_hint;
}
