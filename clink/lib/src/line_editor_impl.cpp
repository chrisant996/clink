// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_impl.h"
#include "line_buffer.h"
#include "match_generator.h"
#include "match_pipeline.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <terminal/terminal_in.h>
#include <terminal/terminal_out.h>

//------------------------------------------------------------------------------
inline char get_closing_quote(const char* quote_pair)
{
    return quote_pair[1] ? quote_pair[1] : quote_pair[0];
}



//------------------------------------------------------------------------------
line_editor* line_editor_create(const line_editor::desc& desc)
{
    // Check there's at least a terminal.
    if (desc.input == nullptr)
        return nullptr;

    if (desc.output == nullptr)
        return nullptr;

    return new line_editor_impl(desc);
}

//------------------------------------------------------------------------------
void line_editor_destroy(line_editor* editor)
{
    delete editor;
}



//------------------------------------------------------------------------------
line_editor_impl::line_editor_impl(const desc& desc)
: m_module(desc.shell_name)
, m_desc(desc)
, m_printer(*desc.output)
{
    if (m_desc.quote_pair == nullptr)
        m_desc.quote_pair = "";

    add_module(m_module);
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

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    m_desc.input->begin();
    m_desc.output->begin();
    m_buffer.begin_line();

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

//------------------------------------------------------------------------------
bool line_editor_impl::get_line(char* out, int out_size)
{
    if (check_flag(flag_editing))
        end_line();

    if (check_flag(flag_eof))
        return false;

    const char* line = m_buffer.get_buffer();
    str_base(out, out_size).copy(line);
    return true;
}

//------------------------------------------------------------------------------
bool line_editor_impl::edit(char* out, int out_size)
{
    // Update first so the init state goes through.
    while (update())
        m_desc.input->select();

    return get_line(out, out_size);
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
void line_editor_impl::update_input()
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
        return;
    }

    if (key < 0)
        return;

    if (!m_bind_resolver.step(key))
        return;

    struct result_impl : public editor_module::result
    {
        enum
        {
            flag_pass       = 1 << 0,
            flag_done       = 1 << 1,
            flag_eof        = 1 << 2,
            flag_redraw     = 1 << 3,
            flag_append_lcd = 1 << 4,
        };

        virtual void    pass() override                           { flags |= flag_pass; }
        virtual void    done(bool eof) override                   { flags |= flag_done|(eof ? flag_eof : 0); }
        virtual void    redraw() override                         { flags |= flag_redraw; }
        virtual void    append_match_lcd() override               { flags |= flag_append_lcd; }
        virtual void    accept_match(unsigned int index) override { match = index; }
        virtual int     set_bind_group(int id) override           { int t = group; group = id; return t; }
        int             match;  // = -1;  <!
        unsigned short  group;  //        <! MSVC bugs; see connect
        unsigned char   flags;  // = 0;   <! issues about C2905
    };

    while (auto binding = m_bind_resolver.next())
    {
        // Binding found, dispatch it off to the module.
        result_impl result;
        result.match = -1;
        result.flags = 0;
        result.group = m_bind_resolver.get_group();

        str<16> chord;
        editor_module* module = binding.get_module();
        unsigned char id = binding.get_id();
        binding.get_chord(chord);

        line_state line = get_linestate();
        editor_module::context context = get_context(line);
        editor_module::input input = { chord.c_str(), id };
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
            return;

        if (result.flags & result_impl::flag_redraw)
            m_buffer.redraw();

        if (result.match >= 0)
            accept_match(result.match);
        else if (result.flags & result_impl::flag_append_lcd)
            append_match_lcd();
    }

    m_buffer.draw();
}

//------------------------------------------------------------------------------
void line_editor_impl::find_command_bounds(const char*& start, int& length)
{
    const char* line_buffer = m_buffer.get_buffer();
    unsigned int line_cursor = m_buffer.get_cursor();

    start = line_buffer;
    length = line_cursor;

    if (m_desc.command_delims == nullptr)
        return;

    str_iter token_iter(start, length);
    str_tokeniser tokens(token_iter, m_desc.command_delims);
    tokens.add_quote_pair(m_desc.quote_pair);
    while (tokens.next(start, length));

    // We should expect to reach the cursor. If not then there's a trailing
    // separator and we'll just say the command starts at the cursor.
    if (start + length != line_buffer + line_cursor)
    {
        start = line_buffer + line_cursor;
        length = 0;
    }
}

//------------------------------------------------------------------------------
void line_editor_impl::collect_words()
{
    m_words.clear();

    const char* line_buffer = m_buffer.get_buffer();
    unsigned int line_cursor = m_buffer.get_cursor();

    const char* command_start;
    int command_length;
    find_command_bounds(command_start, command_length);

    m_command_offset = int(command_start - line_buffer);

    str_iter token_iter(command_start, command_length);
    str_tokeniser tokens(token_iter, m_desc.word_delims);
    tokens.add_quote_pair(m_desc.quote_pair);
    while (1)
    {
        int length = 0;
        const char* start = nullptr;
        str_token token = tokens.next(start, length);
        if (!token)
            break;

        // Add the word.
        unsigned int offset = unsigned(start - line_buffer);
        m_words.push_back();
        *(m_words.back()) = { offset, unsigned(length), 0, token.delim };
    }

    // Add an empty word if the cursor is at the beginning of one.
    word* end_word = m_words.back();
    if (!end_word || end_word->offset + end_word->length < line_cursor)
    {
        m_words.push_back();
        *(m_words.back()) = { line_cursor };
    }

    // Adjust for quotes.
    for (word& word : m_words)
    {
        if (word.length == 0)
            continue;

        const char* start = line_buffer + word.offset;

        int start_quoted = (start[0] == m_desc.quote_pair[0]);
        int end_quoted = 0;
        if (word.length > 1)
            end_quoted = (start[word.length - 1] == get_closing_quote(m_desc.quote_pair));

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
        word.quoted = !!start_quoted;
    }

    // The last word is truncated to the longest length returned by the match
    // generators. This is a little clunky but works well enough.
    line_state line = get_linestate();
    end_word = m_words.back();
    int prefix_length = 0;
    const char* word_start = line_buffer + end_word->offset;
    for (const auto* generator : m_generators)
    {
        int i = generator->get_prefix_length(line);
        prefix_length = max(prefix_length, i);
    }
    end_word->length = min<unsigned int>(prefix_length, end_word->length);
}

//------------------------------------------------------------------------------
void line_editor_impl::accept_match(unsigned int index)
{
    if (index >= m_matches.get_match_count())
        return;

    const char* match = m_matches.get_match(index);
    if (!*match)
        return;

    word end_word = *(m_words.back());
    int word_start = end_word.offset;
    int word_end = end_word.offset + end_word.length;

    const char* buf_ptr = m_buffer.get_buffer();

    str<288> to_insert;
    if (!m_matches.is_prefix_included())
        to_insert.concat(buf_ptr + word_start, end_word.length);
    to_insert << match;

    // TODO: This has not place here and should be done somewhere else.
    // Clean the word if it is a valid file system path.
    if (os::get_path_type(to_insert.c_str()) != os::path_type_invalid)
        path::normalise(to_insert);

    // Does the selected match need quoting?
    bool needs_quote = end_word.quoted;
    for (const char* c = match; *c && !needs_quote; ++c)
        needs_quote = (strchr(m_desc.word_delims, *c) != nullptr);

    // Clear the word.
    m_buffer.remove(word_start, m_buffer.get_cursor());
    m_buffer.set_cursor(word_start);

    // Readd the word plus the match.
    if (needs_quote && !end_word.quoted)
    {
        char quote[2] = { m_desc.quote_pair[0] };
        m_buffer.insert(quote);
    }
    m_buffer.insert(to_insert.c_str());

    // Use a suffix if one's associated with the match, otherwise derive it.
    char match_suffix = m_matches.get_suffix(index);
    char suffix = match_suffix;
    if (!suffix)
    {
        unsigned int match_length = unsigned(strlen(match));

        word match_word = { 0, match_length };
        array<word> match_words(&match_word, 1);
        line_state match_line = { match, match_length, 0, match_words };

        int prefix_length = 0;
        for (const auto* generator : m_generators)
        {
            int i = generator->get_prefix_length(match_line);
            prefix_length = max(prefix_length, i);
        }

        if (prefix_length != match_length)
            suffix = m_desc.word_delims[0];
    }

    // If this match doesn't make a new partial word, close it off
    if (suffix)
    {
        // Add a closing quote on the end if required and only if the suffix
        // did not come from the match.
        if (needs_quote && !match_suffix)
        {
            char quote[2] = { get_closing_quote(m_desc.quote_pair) };
            m_buffer.insert(quote);
        }

        char suffix_str[2] = { suffix };
        m_buffer.insert(suffix_str);
    }
}

//------------------------------------------------------------------------------
void line_editor_impl::append_match_lcd()
{
    str<288> lcd;
    m_matches.get_match_lcd(lcd);

    unsigned int lcd_length = lcd.length();
    if (!lcd_length)
        return;

    unsigned int cursor = m_buffer.get_cursor();

    word end_word = *(m_words.back());
    int word_end = end_word.offset;
    if (!m_matches.is_prefix_included())
        word_end += end_word.length;

    int dx = lcd_length - (cursor - word_end);
    if (dx < 0)
    {
        m_buffer.remove(cursor + dx, cursor);
        m_buffer.set_cursor(cursor + dx);
    }
    else if (dx > 0)
    {
        int start = end_word.offset;
        if (!m_matches.is_prefix_included())
            start += end_word.length;

        m_buffer.remove(start, cursor);
        m_buffer.set_cursor(start);
        m_buffer.insert(lcd.c_str());
    }

    // Prefix a quote if required.
    bool needs_quote = false;
    for (const char* c = lcd.c_str(); *c && !needs_quote; ++c)
        needs_quote = (strchr(m_desc.word_delims, *c) != nullptr);

    for (int i = 0, n = m_matches.get_match_count(); i < n && !needs_quote; ++i)
    {
        const char* match = m_matches.get_match(i) + lcd_length;
        if (match[0])
            needs_quote = (strchr(m_desc.word_delims, match[0]) != nullptr);
    }

    if (needs_quote && !end_word.quoted)
    {
        char quote[2] = { m_desc.quote_pair[0] };
        int cursor = m_buffer.get_cursor();
        m_buffer.set_cursor(end_word.offset);
        m_buffer.insert(quote);
        m_buffer.set_cursor(cursor + 1);
    }
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
    auto& buffer = const_cast<rl_buffer&>(m_buffer);
    auto& pter = const_cast<printer&>(m_printer);
    return { m_desc.prompt, pter, buffer, line, m_matches };
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
    collect_words();

    const word& end_word = *(m_words.back());

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
        pipeline.fill_info();
    }

    next_key.cursor_pos = m_buffer.get_cursor();
    prev_key.value = m_prev_key;

    // Should we sort and select matches?
    if (next_key.value != prev_key.value)
    {
        str<64> needle;
        int needle_start = end_word.offset;
        if (!m_matches.is_prefix_included())
            needle_start += end_word.length;

        const char* buf_ptr = m_buffer.get_buffer();
        needle.concat(buf_ptr + needle_start, next_key.cursor_pos - needle_start);

        if (!needle.empty() && end_word.quoted)
        {
            int i = needle.length();
            if (needle[i - 1] == get_closing_quote(m_desc.quote_pair))
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
