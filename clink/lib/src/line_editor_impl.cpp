// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_impl.h"
#include "line_buffer.h"
#include "match_pipeline.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <terminal/terminal.h>

//------------------------------------------------------------------------------
line_editor* line_editor_create(const line_editor::desc& desc)
{
    // Check there's at least a terminal.
    if (desc.terminal == nullptr)
        return nullptr;

    if (desc.buffer == nullptr)
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
: m_desc(desc)
, m_initialised(false)
, m_begun(false)
, m_prev_key(~0u)
{
    if (m_desc.backend != nullptr)
        add_backend(*m_desc.backend);
}

//------------------------------------------------------------------------------
void line_editor_impl::initialise()
{
    if (m_initialised)
        return;

    static binder* s_binder;          // MODE4
    static editor_backend* s_backend; // MODE4

    s_binder = &m_binder;
    for (auto* backend : m_backends)
    {
        s_backend = backend;
        backend->bind([](const char* chord, unsigned char id) -> bool {
            return s_binder->bind(chord, *s_backend, id);
        });
    }

    m_initialised = true;
}

//------------------------------------------------------------------------------
void line_editor_impl::begin_line()
{
    m_begun = true;

    m_bind_resolver.reset();
    m_keys_size = 0;

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    m_desc.terminal->begin();

    const auto* buffer = m_desc.buffer;
    line_state line = { m_words, buffer->get_buffer(), buffer->get_cursor() };
    editor_backend::context context = make_context(line);
    for (auto backend : m_backends)
        backend->begin_line(m_desc.prompt, context);
}

//------------------------------------------------------------------------------
void line_editor_impl::end_line()
{
    for (auto i = m_backends.rbegin(), n = m_backends.rend(); i != n; ++i)
        i->end_line();

    m_desc.terminal->end();

    m_begun = false;
}

//------------------------------------------------------------------------------
bool line_editor_impl::add_backend(editor_backend& backend)
{
    editor_backend** slot = m_backends.push_back();
    return (slot != nullptr) ? *slot = &backend, true : false;
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
    if (m_begun)
        end_line();

    if (const char* line = m_desc.buffer->get_buffer())
    {
        str_base(out, out_size).copy(line);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
bool line_editor_impl::edit(char* out, int out_size)
{
    // Update first so the init state goes through.
    while (update())
        m_desc.terminal->select();

    return get_line(out, out_size);
}

//------------------------------------------------------------------------------
bool line_editor_impl::update()
{
    if (!m_initialised)
        initialise();

    if (!m_begun)
    {
        begin_line();
        update_internal();
        return true;
    }

    int key = m_desc.terminal->read();
    record_input(key);

    if (!m_bind_resolver.is_resolved())
        m_binder.update_resolver(key, m_bind_resolver);

    dispatch();
    m_desc.buffer->draw();

    if (!m_begun)
        return false;

    if (!m_bind_resolver.is_resolved())
        update_internal();

    return true;
}

//------------------------------------------------------------------------------
void line_editor_impl::record_input(unsigned char key)
{
    if (m_keys_size < sizeof_array(m_keys) - 1)
        m_keys[m_keys_size++] = key;
}

//------------------------------------------------------------------------------
void line_editor_impl::dispatch()
{
    if (!m_bind_resolver.is_resolved())
        return;

    m_keys[m_keys_size] = '\0';

    int id = m_bind_resolver.get_id();

    auto* buffer = m_desc.buffer;
    line_state line = { m_words, buffer->get_buffer(), buffer->get_cursor() };
    editor_backend::context context = make_context(line);
    editor_backend* backend = m_bind_resolver.get_backend();
    editor_backend::result result = backend->on_input(m_keys, id, context);

    m_keys_size = 0;

    // MODE4 : magic shifts and masks
    unsigned char value = result.value & 0xff;
    switch (value)
    {
    case editor_backend::result::done:
        end_line();
        break;

    case editor_backend::result::accept_match:
        accept_match((result.value >> 8) & 0xffff);
        m_bind_resolver.reset();
        break;

    case editor_backend::result::redraw:
        m_desc.buffer->redraw();
        /* fall through */

    case editor_backend::result::next:
        m_bind_resolver.reset();
        break;

    case editor_backend::result::more_input:
        m_bind_resolver.set_id((result.value >> 8) & 0xff);
        break;
    }
}

//------------------------------------------------------------------------------
void line_editor_impl::collect_words()
{
    const char* line_buffer = m_desc.buffer->get_buffer();
    const unsigned int line_cursor = m_desc.buffer->get_cursor();

    m_words.clear();

    str_iter token_iter(line_buffer, line_cursor);
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
        m_words.push_back();
        *(m_words.back()) = { short(start - line_buffer), length, 0, token.delim };
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
            end_quoted = (start[word.length - 1] == m_desc.quote_pair[0]);

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
        word.quoted = !!start_quoted;
    }

    // Adjust the completing word for if it's partial.
    end_word = m_words.back();
    int partial = 0;
    for (int j = end_word->length - 1; j >= 0; --j)
    {
        int c = line_buffer[end_word->offset + j];
        if (strchr(m_desc.partial_delims, c) == nullptr)
            continue;

        partial = j + 1;
        break;
    }
    end_word->length = partial;
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

    line_buffer& buffer = *(m_desc.buffer);
    const char* buf_ptr = buffer.get_buffer();

    str<288> word;
    word.concat(buf_ptr + word_start, end_word.length);
    word << match;

    // Clean the word if it is a valid file system path.
    if (os::get_path_type(word.c_str()) != os::path_type_invalid)
        path::clean(word);

    buffer.remove(word_start, buffer.get_cursor());
    buffer.set_cursor(word_start);
    buffer.insert(word.c_str());

    // If this match doesn't make a new partial word, close it off
    int last_char = int(strlen(match)) - 1;
    if (strchr(m_desc.partial_delims, match[last_char]) == nullptr)
    {
        // Closing quote?
        int pre_offset = end_word.offset - 1;
        if (pre_offset >= 0)
            if (const char* q = strchr(m_desc.quote_pair, buf_ptr[pre_offset]))
                buffer.insert(q[1] ? q + 1 : q);

        buffer.insert(" ");
    }
}

//------------------------------------------------------------------------------
editor_backend::context line_editor_impl::make_context(const line_state& line) const
{
    return { *m_desc.terminal, *m_desc.buffer, line, m_matches };
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
        const auto* buffer = m_desc.buffer;
        line_state line({ m_words, buffer->get_buffer(), buffer->get_cursor() });
        match_pipeline pipeline(m_matches);
        pipeline.reset();
        pipeline.generate(line, m_generators);
        pipeline.fill_info(m_desc.auto_quote_chars);
    }

    next_key.cursor_pos = m_desc.buffer->get_cursor();
    prev_key.value = m_prev_key;

    // Should we sort and select matches?
    if (next_key.value != prev_key.value)
    {
        str<64> needle;
        int needle_start = end_word.offset + end_word.length;
        const char* buffer = m_desc.buffer->get_buffer();
        needle.concat(buffer + needle_start, next_key.cursor_pos - needle_start);

        match_pipeline pipeline(m_matches);
        pipeline.select(needle.c_str());
        pipeline.sort();

        m_prev_key = next_key.value;

        // Tell all the backends that the matches changed.
        line_state line = { m_words, buffer, m_desc.buffer->get_cursor() };
        editor_backend::context context = make_context(line);
        for (auto backend : m_backends)
            backend->on_matches_changed(context);
    }
}
