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
#include <core/settings.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>
extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
extern Keymap _rl_dispatching_keymap;
extern void _rl_keyseq_chain_dispose(void);
}

//------------------------------------------------------------------------------
const int simple_input_states = (RL_STATE_MOREINPUT |
                                 RL_STATE_NSEARCH |
                                 RL_STATE_CHARSEARCH);

extern setting_bool g_classify_words;

extern bool is_showing_argmatchers();



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
    return m_ptr && m_len == len && memcmp(s, m_ptr, len) == 0 && !m_ptr[len];
}



//------------------------------------------------------------------------------
static line_editor_impl* s_editor = nullptr;

//------------------------------------------------------------------------------
void reset_generate_matches()
{
    if (!s_editor)
        return;

    s_editor->reset_generate_matches();
}

//------------------------------------------------------------------------------
void update_matches()
{
    if (!s_editor)
        return;

    s_editor->update_matches();
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
: m_module(desc.shell_name, desc.input, desc.state_dir)
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
    m_prev_key.reset();

    assert(!s_editor);
    s_editor = this;

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    m_desc.input->begin();
    m_desc.output->begin();
    m_buffer.begin_line();
    m_prev_generate.clear();
    m_prev_classify.clear();

    rl_before_display_function = before_display;

    editor_module::context context = get_context();
    for (auto module : m_modules)
        module->on_begin_line(context);
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
    if (!check_flag(flag_editing))
        m_insert_on_begin = out.c_str();

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
void line_editor_impl::update_matches()
{
    // Get flag states because we're about to clear them.
    bool generate = check_flag(flag_generate);
    bool select = check_flag(flag_select);

    // Clear flag states before running generators, so that generators can use
    // reset_generate_matches().
    clear_flag(flag_generate);
    clear_flag(flag_select);

    if (generate)
    {
        line_state line = get_linestate();
        match_pipeline pipeline(m_matches);
        pipeline.reset();
        pipeline.generate(line, m_generators);
    }

    if (select)
    {
        match_pipeline pipeline(m_matches);
        pipeline.select(m_needle.c_str());
        pipeline.sort();
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
// Readline is designed for raw terminal input, and Windows is capable of richer
// input analysis where we can avoid generating terminal input if there's no
// binding that can handle it.
//
// WARNING:  Violates abstraction and encapsulation; neither rl_ding nor
// _rl_keyseq_chain_dispose make sense in an "is bound" method.  But really this
// is more like "accept_input_key" with the ability to reject an input key, and
// rl_ding or _rl_keyseq_chain_dispose only happen on rejection.  So it's
// functionally reasonable.  Really rl_module should be making the accept/reject
// decision, not line_editor_impl.  But line_editor_impl always has an
// rl_module, and rl_module is the only module that needs to accept/reject keys,
// so it's just wasteful routing the question through other modules.
//
// The trouble is, Readline doesn't natively have a way to reset the dispatching
// state other than rl_abort() or actually dispatching an invalid key sequence.
// So we have to reverse engineer how Readline responds when a key sequence is
// terminated by invalid input, and that seems to consist of clearing the
// RL_STATE_MULTIKEY state and disposing of the key sequence chain.
bool line_editor_impl::is_bound(const char* seq, int len)
{
    if (!len)
    {
LNope:
        if (RL_ISSTATE (RL_STATE_MULTIKEY))
        {
            RL_UNSETSTATE(RL_STATE_MULTIKEY);
            _rl_keyseq_chain_dispose();
        }
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

    // NOTE:  Checking readline's keymap is incorrect when a special bind group
    // is active that should block on_input from reaching readline.  But the way
    // that blocking is achieved is by adding a "" binding that matches
    // everything not explicitly bound in the keymap.  So it works out
    // naturally, without additional effort.  But it would probably be cleaner
    // for this to be implemented on rl_module rather than line_editor_impl.

    // Using nullptr for the keymap starts from the root of the current keymap,
    // but in a multi key sequence this needs to use the current dispatching
    // node of the current keymap.
    Keymap keymap = RL_ISSTATE (RL_STATE_MULTIKEY) ? _rl_dispatching_keymap : nullptr;
    if (rl_function_of_keyseq_len(seq, len, keymap, nullptr))
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
        virtual void    invalid() override                        { flags |= flag_invalid; }
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
            editor_module::input input = { chord.c_str(), chord.length(), id };
            module->on_input(input, result, context);
        }

        m_bind_resolver.set_group(result.group);

        // Process what result_impl has collected.

        if (binding) // May have been claimed already by dispatch() inside on_input().
        {
            if (result.flags & result_impl::flag_pass)
                continue;
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

        // First try to accommodate `-flag:text` and `-flag=text` (with or
        // without quotes) so that matching can happen for the `text` portion.
        if (strchr("-/", *word_start))
        {
            for (const char* walk = word_start; *walk && !isspace((unsigned char)*walk); walk++)
            {
                if (strchr(":=", *walk))
                {
                    if (walk[1] &&
                        (rl_completer_quote_characters && strchr(rl_completer_quote_characters, walk[1])) ||
                        (rl_basic_quote_characters && strchr(rl_basic_quote_characters, walk[1])))
                        walk++;
                    break_info.truncate = walk + 1 - word_start;
                    break_info.keep = end_word->length - break_info.truncate;
                    break;
                }
            }
        }

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
            split_word.is_alias = false;
            split_word.quoted = false;
            split_word.delim = str_token::invalid_delim;

            end_word->length = truncate;
            words.push_back(split_word);
            end_word = &words.back();
        }

        int keep = min<unsigned int>(break_info.keep, end_word->length);
        end_word->length = keep;

        // Need to coordinate with Readline when we redefine word breaks.
        matches.set_word_break_position(end_word->offset);
    }

    return command_offset;
}

//------------------------------------------------------------------------------
void line_editor_impl::classify()
{
    if (!m_classifier)
        return;

    // Skip parsing if the line buffer hasn't changed.
    if (m_prev_classify.equals(m_buffer.get_buffer(), m_buffer.get_length()))
        return;

    // Use the full line; don't stop at the cursor.
    line_state line = get_linestate();
    collect_words(false);

    // Hang on to the old classifications so it's possible to detect changes.
    word_classifications old_classifications(std::move(m_classifications));
    m_classifications.init(strlen(line.get_line()));

    // Count number of commands so we can pre-allocate words_storage so that
    // emplace_back() doesn't invalidate pointers (references) stored in
    // linestates.
    unsigned int num_commands = 0;
    for (const auto& word : m_words)
    {
        if (word.command_word)
            num_commands++;
    }

    // Build vector containing one line_state per command.
    int i = 0;
    int command_word_offset = 0;
    std::vector<word> words;
    std::vector<std::vector<word>> words_storage;
    std::vector<line_state> linestates;
    words_storage.reserve(num_commands);
    while (true)
    {
        if (!words.empty() && (i >= m_words.size() || m_words[i].command_word))
        {
            // Make sure classifiers can tell whether the word has a space
            // before it, so that ` doskeyalias` gets classified as NOT a doskey
            // alias, since doskey::resolve() won't expand it as a doskey alias.
            int command_char_offset = words[0].offset;
            if (command_char_offset == 1 && m_buffer.get_buffer()[0] == ' ')
                command_char_offset--;
            else if (command_char_offset >= 2 &&
                     m_buffer.get_buffer()[command_char_offset - 1] == ' ' &&
                     m_buffer.get_buffer()[command_char_offset - 2] == ' ')
                command_char_offset--;

            words_storage.emplace_back(std::move(words));
            assert(words.empty());

            linestates.emplace_back(
                m_buffer.get_buffer(),
                m_buffer.get_cursor(),
                command_char_offset,
                words_storage.back()
            );

            command_word_offset = i;
        }

        if (i >= m_words.size())
            break;

        words.push_back(m_words[i]);
        i++;
    }

    m_classifier->classify(linestates, m_classifications);
    m_classifications.finish(is_showing_argmatchers());

#ifdef DEBUG
    if (dbg_get_env_int("DEBUG_CLASSIFY"))
    {
        static const char *const word_class_name[] = {"other", "command", "doskey", "arg", "flag", "none"};
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
editor_module::context line_editor_impl::get_context() const
{
    auto& pter = const_cast<printer&>(m_printer);
    auto& pger = const_cast<pager&>(static_cast<const pager&>(m_pager));
    auto& buffer = const_cast<rl_buffer&>(m_buffer);
    return { m_desc.prompt, pter, pger, buffer, m_matches, m_classifications };
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
    pipeline.set_nosort(nosort);

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
            match_pipeline pipeline(m_matches);
            pipeline.reset();
            // Defer generating until update_matches().  Must set word break
            // position in the meantime because adjust_completion_word() gets
            // called before the deferred generate().
            set_flag(flag_generate);
            m_matches.set_word_break_position(line.get_end_word_offset());
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

    // Must defer updating m_prev_generate since the old value is still needed
    // for deciding whether to sort/select, after deciding whether to generate.
    if (update_prev_generate >= 0)
        m_prev_generate.set(m_buffer.get_buffer(), update_prev_generate);

    if (is_endword_tilde(get_linestate()))
        reset_generate_matches();
}

void line_editor_impl::before_display()
{
    assert(s_editor);
    if (s_editor)
        s_editor->classify();
}

matches* maybe_regenerate_matches(const char* needle, bool popup)
{
    if (!s_editor || s_editor->m_matches.is_regen_blocked())
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
