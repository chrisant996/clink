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
}

//------------------------------------------------------------------------------
extern setting_bool g_classify_words;
extern setting_bool g_autosuggest_async;
extern int g_suggestion_offset;

extern "C" void host_clear_suggestion();
extern void reset_suggester();
extern bool check_recognizer_refresh();
extern bool is_showing_argmatchers();
extern bool win_fn_callback_pending();
extern recognition recognize_command(const char* line, const char* word, bool quoted, bool& ready, str_base* file=nullptr);
extern std::shared_ptr<match_builder_toolkit> get_deferred_matches(int generation_id);



//------------------------------------------------------------------------------
inline char get_closing_quote(const char* quote_pair)
{
    return quote_pair[1] ? quote_pair[1] : quote_pair[0];
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
void prev_buffer::set(const char* s, int len)
{
    free(m_ptr);

    m_ptr = (char*)malloc(len + 1);
    m_len = len;
    memcpy(m_ptr, s, len);
    m_ptr[len] = '\0';
}

//------------------------------------------------------------------------------
bool prev_buffer::equals(const char* s, int len) const
{
    return m_ptr && m_len == static_cast<unsigned int>(len) && memcmp(s, m_ptr, len) == 0 && !m_ptr[len];
}



//------------------------------------------------------------------------------
static line_editor_impl* s_editor = nullptr;
static host_callbacks* s_callbacks = nullptr;
word_collector* g_word_collector = nullptr;

//------------------------------------------------------------------------------
void reset_generate_matches()
{
    if (!s_editor)
        return;

    s_editor->reset_generate_matches();
}

//------------------------------------------------------------------------------
bool is_regen_blocked()
{
    return (s_editor && s_editor->m_matches.is_regen_blocked());
}

//------------------------------------------------------------------------------
void force_update_internal(bool restrict)
{
    if (!s_editor)
        return;

    s_editor->force_update_internal(restrict);
}

//------------------------------------------------------------------------------
void update_matches()
{
    if (!s_editor)
        return;

    s_editor->update_matches();
}

//------------------------------------------------------------------------------
bool notify_matches_ready(int generation_id)
{
    if (!s_editor)
        return false;

    auto toolkit = get_deferred_matches(generation_id);
    matches* matches = toolkit ? toolkit->get_matches() : nullptr;
    return s_editor->notify_matches_ready(generation_id, matches);
}

//------------------------------------------------------------------------------
void set_prompt(const char* prompt, const char* rprompt, bool redisplay)
{
    if (!s_editor)
        return;

    s_editor->set_prompt(prompt, rprompt, redisplay);
}



//------------------------------------------------------------------------------
int host_add_history(int, const char* line)
{
    if (!s_callbacks)
        return 0;

    return s_callbacks->add_history(line);
}

//------------------------------------------------------------------------------
int host_remove_history(int rl_history_index, const char* line)
{
    if (!s_callbacks)
        return 0;

    return s_callbacks->remove_history(rl_history_index, line);
}

//------------------------------------------------------------------------------
void host_filter_prompt()
{
    if (!s_callbacks)
        return;

    s_callbacks->filter_prompt();
}

//------------------------------------------------------------------------------
extern "C" void host_filter_transient_prompt(int crlf)
{
    if (!s_callbacks)
        return;

    s_callbacks->filter_transient_prompt(crlf < 0/*final*/);
}

//------------------------------------------------------------------------------
bool host_can_suggest(line_state& line)
{
    if (!s_callbacks)
        return false;

    return s_callbacks->can_suggest(line);
}

//------------------------------------------------------------------------------
int host_filter_matches(char** matches)
{
    if (s_callbacks)
        s_callbacks->filter_matches(matches);
    return 0;
}

//------------------------------------------------------------------------------
void host_invalidate_matches()
{
    reset_generate_matches();
    host_clear_suggestion();
    if (s_editor)
        s_editor->try_suggest();
}

//------------------------------------------------------------------------------
void host_send_event(const char* event_name)
{
    if (!s_callbacks)
        return;

    s_callbacks->send_event(event_name);
}

//------------------------------------------------------------------------------
bool host_call_lua_rl_global_function(const char* func_name)
{
    return s_editor && s_editor->call_lua_rl_global_function(func_name);
}

//------------------------------------------------------------------------------
const char** host_copy_dir_history(int* total)
{
    if (!s_callbacks)
        return nullptr;

    return s_callbacks->copy_dir_history(total);
}

//------------------------------------------------------------------------------
void host_get_app_context(int& id, str_base& binaries, str_base& profile, str_base& scripts)
{
    if (!s_callbacks)
        return;

    s_callbacks->get_app_context(id, binaries, profile, scripts);
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
    for (unsigned int offset = line.get_end_word_offset(); true; ++offset)
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

        virtual bool bind(unsigned int group, const char* chord, unsigned char key, bool has_params=false) override
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

    m_bind_resolver.reset();
    m_command_offset = 0;
    m_keys_size = 0;
    m_prev_key.reset();

    assert(!s_editor);
    assert(!g_word_collector);
    s_editor = this;
    s_callbacks = m_desc.callbacks;
    g_word_collector = &m_collector;

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    m_desc.input->begin();
    m_desc.output->begin();
    m_buffer.begin_line();
    m_prev_generate.clear();
    m_prev_classify.clear();
    m_prev_command_word.clear();
    m_prev_command_word_quoted = false;

    m_words.clear();
    m_commands.clear();
    m_classify_words.clear();

    rl_before_display_function = before_display;

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
    for (auto i = m_modules.rbegin(), n = m_modules.rend(); i != n; ++i)
        i->on_end_line();

    rl_before_display_function = nullptr;

    m_buffer.end_line();
    m_desc.output->end();
    m_desc.input->end();

    m_words.clear();
    m_commands.clear();
    m_classify_words.clear();

    s_editor = nullptr;
    s_callbacks = nullptr;
    g_word_collector = nullptr;

    reset_suggester();

    clear_flag(flag_editing);

    assert(!m_in_matches_ready);
}

//------------------------------------------------------------------------------
bool line_editor_impl::add_module(editor_module& module)
{
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
void line_editor_impl::set_prompt(const char* prompt, const char* rprompt, bool redisplay)
{
    m_desc.prompt = prompt;
    m_module.set_prompt(prompt, rprompt, redisplay);
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
bool line_editor_impl::edit(str_base& out)
{
    if (!check_flag(flag_editing))
        m_insert_on_begin = out.c_str();

    // Update first so the init state goes through.
    while (update())
    {
        if (!m_module.is_input_pending())
            m_desc.input->select(m_idle);
    }

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
void line_editor_impl::force_update_internal(bool restrict)
{
    update_internal();
    if (restrict) set_flag(flag_restrict);
    set_flag(flag_select);
}

//------------------------------------------------------------------------------
bool line_editor_impl::notify_matches_ready(int generation_id, matches* matches)
{
#ifdef DEBUG
    assert(!m_in_matches_ready);
    rollback<bool> rb(m_in_matches_ready, true);
#endif

    // The generation matches, then use the newly generated matches.
    if (matches && generation_id == m_generation_id)
    {
        assert(&m_matches != matches);
        m_matches.done_building();
        m_matches.transfer(*(matches_impl*)matches);
        clear_flag(flag_generate);
    }
    else
    {
        // There's a newer generation id, so force a new suggestion, since the
        // newer generation id's autosuggest will have been canceled due to the
        // match generator coroutine that was already running, and which has
        // just now signaled its completion.
        host_clear_suggestion();
    }

    // Trigger generating suggestion again.
    try_suggest();
    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::update_matches()
{
    // Get flag states because we're about to clear them.
    bool generate = check_flag(flag_generate);
    bool restrict = check_flag(flag_restrict);
    bool select = generate || restrict || check_flag(flag_select);

    // Clear flag states before running generators, so that generators can use
    // reset_generate_matches().
    clear_flag(flag_generate);
    clear_flag(flag_restrict);
    clear_flag(flag_select);

    if (generate)
    {
        line_state line = get_linestate();
        match_pipeline pipeline(m_matches);
        pipeline.reset();
        pipeline.generate(line, m_generator);
    }

    if (restrict)
    {
        match_pipeline pipeline(m_matches);

        // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.  Stripping
        // quotes may seem surprising, but it's what CMD does and it works well.
        str<> tmp;
        concat_strip_quotes(tmp, m_needle.c_str(), m_needle.length());

        bool just_tilde = false;
        if (rl_complete_with_tilde_expansion)
        {
            char* expanded = tilde_expand(tmp.c_str());
            if (expanded && strcmp(tmp.c_str(), expanded) != 0)
            {
                just_tilde = (tmp.c_str()[0] == '~' && tmp.c_str()[1] == '\0');
                tmp = expanded;
            }
            free(expanded);
        }

        m_needle = tmp.c_str();
        if (!is_literal_wild() && !just_tilde)
            m_needle.concat("*", 1);
        pipeline.restrict(m_needle);
    }

    if (select)
    {
        match_pipeline pipeline(m_matches);
        pipeline.select(m_needle.c_str());
        pipeline.sort();
    }

    // Tell all the modules that the matches changed.
    if (generate || restrict || select)
    {
        line_state line = get_linestate();
        editor_module::context context = get_context();
        for (auto module : m_modules)
            module->on_matches_changed(context, line, m_needle.c_str());
    }
}

//------------------------------------------------------------------------------
void line_editor_impl::dispatch(int bind_group)
{
    assert(check_flag(flag_init));
    assert(check_flag(flag_editing));

    // Claim any pending binding, otherwise we'll try to dispatch it again.

    if (m_pending_binding)
        m_pending_binding->claim();

    // Handle one input.

    const int prev_bind_group = m_bind_resolver.get_group();
    m_bind_resolver.set_group(bind_group);

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
bool line_editor_impl::is_bound(const char* seq, int len)
{
    // Check if clink has a binding; these override Readline.
    int bound = m_bind_resolver.is_bound(seq, len);
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
bool line_editor_impl::translate(const char* seq, int len, str_base& out)
{
    return m_module.translate(seq, len, out);
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
    if (!m_module.is_input_pending())
    {
        int key = m_desc.input->read();

        if (key == terminal_in::input_terminal_resize)
        {
            int columns = m_desc.output->get_columns();
            int rows = m_desc.output->get_rows();
            editor_module::context context = get_context();
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
        virtual int     set_bind_group(int id) override           { int t = group; group = id; return t; }
        unsigned short  group;  //        <! MSVC bugs; see connect
        unsigned char   flags;  // = 0;   <! issues about C2905
    };

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

        {
            rollback<bind_resolver::binding*> _(m_pending_binding, &binding);

            editor_module::context context = get_context();
            editor_module::input input = { chord.c_str(), chord.length(), id, binding.get_params() };
            module->on_input(input, result, context);
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
            // Classify words in the input line (if configured).
            if (g_classify_words.get())
                classify();

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
    m_command_offset = collect_words(m_words, &m_matches, collect_words_mode::stop_at_cursor, m_commands);
}

//------------------------------------------------------------------------------
commands line_editor_impl::collect_commands()
{
    commands commands;
    collect_words(m_classify_words, nullptr, collect_words_mode::whole_command, commands);
    return commands;
}

//------------------------------------------------------------------------------
unsigned int line_editor_impl::collect_words(words& words, matches_impl* matches, collect_words_mode mode, commands& commands)
{
    unsigned int command_offset = m_collector.collect_words(m_buffer, words, mode);
    commands.set(m_buffer, words);

#ifdef DEBUG
    const int dbg_row = dbg_get_env_int("DEBUG_COLLECTWORDS");
    str<> tmp1;
    str<> tmp2;
    if (dbg_row > 0)
    {
        if (words.size() > 0)
        {
            tmp1.format("\x1b[s\x1b[%dHcollected words:        ", dbg_row);
            m_printer.print(tmp1.c_str(), tmp1.length());
            for (auto const& w : words)
            {
                const char* q = w.quoted ? "\"" : "";
                if (w.is_redir_arg)
                    m_printer.print(">");
                if (w.command_word)
                    m_printer.print("!");
                if (w.length)
                    tmp1.format("%s\x1b[0;37;7m%.*s\x1b[m%s ", q, w.length, m_buffer.get_buffer() + w.offset, q);
                else
                    tmp1.format("\x1b[0;37;7m \x1b[m ");
                m_printer.print(tmp1.c_str(), tmp1.length());
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
    // breaks. This is a little clunky but works well enough.
    if (words.back().length && mode == collect_words_mode::stop_at_cursor)
    {
        word_break_info break_info;
        if (m_generator)
            m_generator->get_word_break_info(commands.get_linestate(m_buffer), break_info);
        const unsigned int end_word_offset = commands.break_end_word(break_info.truncate, break_info.keep);

#ifdef DEBUG
        if (dbg_row > 0)
        {
            int i_word = 1;
            tmp2.format("\x1b[s\x1b[%dHafter word break info:  ", dbg_row + 1);
            m_printer.print(tmp2.c_str(), tmp2.length());
            for (auto const& w : commands.get_linestate(m_buffer).get_words())
            {
                const char* q = w.quoted ? "\"" : "";
                if (w.is_redir_arg)
                    m_printer.print(">");
                if (w.command_word)
                    m_printer.print("!");
                const char* color = (i_word == words.size()) ? "35;7" : "37;7";
                const char* delim = (i_word + 1 == words.size()) ? "" : " ";
                tmp2.format("%s\x1b[0;%sm%.*s\x1b[m%s", q, color, w.length, m_buffer.get_buffer() + w.offset, q, delim);
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

    return command_offset;
}

//------------------------------------------------------------------------------
void line_editor_impl::classify()
{
    if (!m_classifier)
        return;

    rollback<int> rb_end(rl_end);
    if (g_suggestion_offset >= 0)
        rl_end = g_suggestion_offset;

    // Skip parsing if the line buffer hasn't changed.
    if (m_prev_classify.equals(m_buffer.get_buffer(), m_buffer.get_length()))
        return;

    // Hang on to the old classifications so it's possible to detect changes.
    word_classifications old_classifications(std::move(m_classifications));
    m_classifications.init(m_buffer.get_length(), &old_classifications);

    // Use the full line; don't stop at the cursor.
    commands commands = collect_commands();
    m_classifier->classify(commands.get_linestates(), m_classifications);
    m_classifications.finish(is_showing_argmatchers());

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_CLASSIFY"))
    {
        static const char *const word_class_name[] = {"other", "unrecognized", "executable", "command", "doskey", "arg", "flag", "none"};
        static_assert(sizeof_array(word_class_name) == int(word_class::max), "word_class flag count mismatch");
        printf("CLASSIFIED '%s' -- ", m_buffer.get_buffer());
        word_class wc;
        for (unsigned int i = 0; i < m_classifications.size(); ++i)
        {
            if (m_classifications.get_word_class(i, wc))
                printf(" %d:%s", i, word_class_name[int(wc)]);
        }
        printf("\n");
    }
#endif

    m_prev_classify.set(m_buffer.get_buffer(), m_buffer.get_length());

    if (!old_classifications.equals(m_classifications))
        m_buffer.set_need_draw();
}

//------------------------------------------------------------------------------
void line_editor_impl::maybe_send_oncommand_event()
{
    if (!m_desc.callbacks)
        return;

    line_state line = get_linestate();
    if (line.get_word_count() <= 1)
        return;

    const word& info = line.get_words()[0];
    if (m_prev_command_word_quoted == info.quoted &&
        m_prev_command_word.length() == info.length &&
        _strnicmp(m_prev_command_word.c_str(), m_buffer.get_buffer() + info.offset, info.length) == 0)
        return;

    str<> first_word;
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
            int length;
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
        if (!ready)
            return;

        m_desc.callbacks->send_oncommand_event(line, lookup, quoted, recognized, file.c_str());
    }

    m_prev_command_word = first_word.c_str();
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

    if (refresh || why == reclassify_reason::force)
    {
        m_prev_classify.clear();
        m_buffer.set_need_draw();
        m_buffer.draw();
    }
}

//------------------------------------------------------------------------------
void host_reclassify(reclassify_reason why)
{
    if (s_editor)
        s_editor->reclassify(why);
}

//------------------------------------------------------------------------------
void host_refresh_recognizer()
{
    if (s_editor)
        s_editor->reclassify(reclassify_reason::recognizer);
}

//------------------------------------------------------------------------------
line_state line_editor_impl::get_linestate() const
{
    return m_commands.get_linestate(m_buffer);
}

//------------------------------------------------------------------------------
editor_module::context line_editor_impl::get_context() const
{
    auto& pter = const_cast<printer&>(m_printer);
    auto& pger = const_cast<pager&>(static_cast<const pager&>(m_pager));
    auto& buffer = const_cast<rl_buffer&>(m_buffer);
    return { m_desc.prompt, m_desc.rprompt, pter, pger, buffer, m_matches, m_classifications };
}

//------------------------------------------------------------------------------
void line_editor_impl::set_flag(unsigned char flag)
{
    m_flags |= flag;

    if (flag & flag_generate)
        m_generation_id = max<int>(m_generation_id + 1, 1);
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
bool line_editor_impl::is_key_same(const key_t& prev_key, const char* prev_line, int prev_length,
                                   const key_t& next_key, const char* next_line, int next_length,
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
    str_iter prev(prev_line + prev_key.word_offset, min(int(prev_key.word_length), max(0, int(prev_length - prev_key.word_offset))));
    str_iter next(next_line + next_key.word_offset, min(int(next_key.word_length), max(0, int(next_length - next_key.word_offset))));
    for (int i = prev.length(); i--;)
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
    const key_t next_key = { (unsigned int)m_words.size() - 1, end_word.offset, end_word.length, m_buffer.get_cursor() };

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

matches* get_mutable_matches(bool nosort=false)
{
    if (!s_editor)
        return nullptr;

    return s_editor->get_mutable_matches(nosort);
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

    const key_t next_key = { (unsigned int)m_words.size() - 1, end_word.offset, end_word.length, m_buffer.get_cursor() };
    const key_t prev_key = m_prev_key;

    // Should we generate new matches?  Matches are generated for the end word
    // position.  If the end word hasn't changed, then don't generate matches.
    // Since the end word is empty, don't compare the cursor position, so the
    // matches are only collected once for the word position.
    int update_prev_generate = -1;
    if (!is_key_same(prev_key, m_prev_generate.get(), m_prev_generate.length(),
                     next_key, m_buffer.get_buffer(), m_buffer.get_length(),
                     false/*compare_cursor*/))
    {
        line_state line = get_linestate();
        str_iter end_word = line.get_end_word();
        int len = int(end_word.get_pointer() + end_word.length() - line.get_line());
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
        int needle_start = end_word.offset;
        const char* buf_ptr = m_buffer.get_buffer();

        // Strip quotes so `"foo\"ba` can complete to `"foo\bar"`.  Stripping
        // quotes may seem surprising, but it's what CMD does and it works well.
        m_needle.clear();
        concat_strip_quotes(m_needle, buf_ptr + needle_start, next_key.cursor_pos - needle_start);

        if (!m_needle.empty() && end_word.quoted)
        {
            int i = m_needle.length();
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
    line_state line = get_linestate();
    if (host_can_suggest(line))
    {
        matches_impl* matches = nullptr;
        matches_impl* empty_matches = nullptr;

        if (m_words.size())
        {
            const word& word = m_words[m_words.size() - 1];
            if (word.offset < m_buffer.get_length() && m_buffer.get_length() - word.offset >= 2)
            {
                // Use an empty set of matches when a UNC path is detected.
                // Or when a remote drive is detected (or unknown or invalid).
                // Removable drives are accepted because typically these are
                // thumb drives these days, which are fast.
                const char* end_word = m_buffer.get_buffer() + word.offset;
                bool no_matches = (path::is_separator(end_word[0]) && path::is_separator(end_word[1]));
                if (!no_matches)
                {
                    str<> full;
                    if (os::get_full_path_name(end_word, full, m_buffer.get_length() - word.offset))
                    {
                        path::get_drive(full);
                        path::append(full, ""); // Because get_drive_type() requires a trailing path separator.
                        no_matches = (os::get_drive_type(full.c_str()) < os::drive_type_removable);
                    }
                }
                if (no_matches)
                    matches = empty_matches = new matches_impl;
            }
        }

        // Never generate matches here; let it be deferred and happen on demand
        // in a coroutine.
        if (!empty_matches && (!check_flag(flag_generate) || !g_autosuggest_async.get()))
        {
            update_matches();
            matches = &m_matches;
        }

        assert(s_callbacks); // Was tested above inside host_can_suggest().
        s_callbacks->suggest(line, matches, m_generation_id);

        delete empty_matches;
    }
}

//------------------------------------------------------------------------------
void line_editor_impl::before_display()
{
    assert(s_editor);
    if (s_editor)
        s_editor->classify();
}

//------------------------------------------------------------------------------
bool line_editor_impl::call_lua_rl_global_function(const char* func_name)
{
    if (!s_callbacks)
        return false;

    line_state line = get_linestate();
    return s_callbacks->call_lua_rl_global_function(func_name, &line);
}

//------------------------------------------------------------------------------
matches* maybe_regenerate_matches(const char* needle, display_filter_flags flags)
{
    if (!s_editor || s_editor->m_matches.is_regen_blocked())
        return nullptr;

    // Check if a match display filter is active.
    matches_impl& regen = s_editor->m_regen_matches;
    bool old_filtering = false;
    if (!regen.match_display_filter(nullptr, nullptr, nullptr, flags, &old_filtering))
        return nullptr;

#ifdef DEBUG
    int debug_filter = dbg_get_env_int("DEBUG_FILTER");
    if (debug_filter) puts("REGENERATE_MATCHES");
#endif

    commands commands;
    std::vector<word> words;
    unsigned int command_offset = s_editor->collect_words(words, &regen, collect_words_mode::stop_at_cursor, commands);
    line_state line = commands.get_linestate(s_editor->m_buffer);

    match_pipeline pipeline(regen);
    pipeline.reset();

#ifdef DEBUG
    if (debug_filter) puts("-- GENERATE");
#endif

    pipeline.generate(line, s_editor->m_generator, old_filtering);

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

    if (old_filtering)
    {
        // Using old_filtering lets deprecated generators filter based on the
        // input needle.  That poisons the collected matches for any other use,
        // so the matches must be reset.
        reset_generate_matches();
    }

    return &regen;
}
