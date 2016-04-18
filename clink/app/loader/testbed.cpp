#include "pch.h"
#include "rl/rl_backend.h"

#include <core/array.h>
#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <line_state.h>
#include <src/bind_resolver.h>
#include <lib/binder.h>
#include <lib/editor_backend.h>
#include <lib/match_generator.h>
#include <matches/match_pipeline.h>
#include <matches/matches.h>
#include <terminal/win_terminal.h>

class line_buffer;
class editor_backend;

//------------------------------------------------------------------------------
static setting_int g_query_threshold(
    "match.query_threshold",
    "Ask if matches > threshold",
    "", // MODE4
    100);

static setting_int g_max_width(
    "match.max_width",
    "Maximum display width",
    "", // MODE4
    106);

static setting_bool g_vertical(
    "match.vertical",
    "Display matches vertically",
    "", // MODE4
    true);



//------------------------------------------------------------------------------
class classic_match_ui
    : public editor_backend
{
private:
    enum state : unsigned char
    {
        state_none,
        state_query,
        state_pager,
        state_print,
        state_print_one,
        state_print_page,
    };

    virtual void    bind(binder& binder) override;
    virtual void    begin_line(const char* prompt, const context& context) override;
    virtual void    end_line() override;
    virtual void    on_matches_changed(const context& context) override;
    virtual result  on_input(const char* keys, int id, const context& context) override;
    state           begin_print(const context& context);
    state           print(const context& context, bool single_row);
    state           query_prompt(unsigned char key, const context& context);
    state           pager_prompt(unsigned char key, const context& context);
    bool            m_waiting = false;
    int             m_longest = 0;
    int             m_row = 0;
};

//------------------------------------------------------------------------------
void classic_match_ui::bind(binder& binder)
{
    binder.bind("\t", *this, state_none);
}

//------------------------------------------------------------------------------
void classic_match_ui::begin_line(const char* prompt, const context& context)
{
}

//------------------------------------------------------------------------------
void classic_match_ui::end_line()
{
}

//------------------------------------------------------------------------------
void classic_match_ui::on_matches_changed(const context& context)
{
    m_waiting = false;
}

//------------------------------------------------------------------------------
editor_backend::result classic_match_ui::on_input(
    const char* keys,
    int id,
    const context& context)
{
    auto& terminal = context.terminal;
    auto& matches = context.matches;

    if (m_waiting)
    {
        int next_state = state_none;
        switch (id)
        {
        case state_none:    next_state = begin_print(context); break;
        case state_query:   next_state = query_prompt(keys[0], context); break;
        case state_pager:   next_state = pager_prompt(keys[0], context); break;
        }

        if (next_state > state_print)
            next_state = print(context, next_state == state_print_one);

        switch (next_state)
        {
        case state_query:   return { result::more_input, state_query };
        case state_pager:   return { result::more_input, state_pager };
        }

        return result::redraw;
    }

    // One match? Accept it.
    if (matches.get_match_count() == 1)
        return { result::accept_match, 0 };

    // Valid LCD? Append it.
    str<> lcd;
    matches.get_match_lcd(lcd);
    if (int lcd_length = lcd.length())
    {
        const word end_word = *(context.line.get_words().back());

        line_buffer& buffer = context.buffer;
        unsigned int cursor = buffer.get_cursor();

        int word_end = end_word.offset + end_word.length;
        int dx = lcd_length - (cursor - word_end);

        if (dx < 0)
        {
            buffer.remove(cursor + dx, cursor);
            buffer.set_cursor(cursor + dx);
        }
        else if (dx > 0)
            buffer.insert(lcd.c_str() + lcd_length - dx);
        else if (!dx)
            m_waiting = true;

        return result::next;
    }

    m_waiting = true;
    return result::next;
}

//------------------------------------------------------------------------------
classic_match_ui::state classic_match_ui::begin_print(const context& context)
{
    const matches& matches = context.matches;
    int match_count = matches.get_match_count();

    m_longest = 0;
    m_row = 0;

    // Get the longest match length.
    for (int i = 0; i < matches.get_match_count(); ++i)
    {
        const char* match = matches.get_match(i);
        m_longest = max<int>(char_count(match), m_longest);
    }

    if (!m_longest)
        return state_none;

    context.terminal.write("\n", 1);

    int query_threshold = g_query_threshold.get();
    if (query_threshold > 0 && query_threshold <= match_count)
    {
        str<64> prompt;
        prompt.format("Show %d matches? [Yn]", match_count);
        context.terminal.write(prompt.c_str(), -1);
        context.terminal.flush();

        return state_query;
    }

    return state_print_page;
}

//------------------------------------------------------------------------------
classic_match_ui::state classic_match_ui::print(const context& context, bool single_row)
{
    terminal& term = context.terminal;
    const matches& matches = context.matches;

    auto_flush flusher(term);
    term.write("\r", 1);

    int match_count = matches.get_match_count();

    int columns = max(1, g_max_width.get() / m_longest);
    int total_rows = (match_count + columns - 1) / columns;

    bool vertical = g_vertical.get();
    int dx = vertical ? total_rows : 1;

    int max_rows = single_row ? 1 : (total_rows - m_row - 1);
    max_rows = min(term.get_rows() - 1 - !!m_row, max_rows);
    for (; max_rows >= 0; --max_rows, ++m_row)
    {
        int index = vertical ? m_row : (m_row * columns);
        for (int x = 0; x < columns; ++x)
        {
            if (index >= match_count)
                continue;

            const char* match = matches.get_match(index);
            term.write(match, int(strlen(match)));

            for (int i = m_longest - char_count(match) + 1; i >= 0;)
            {
                const char spaces[] = "                ";
                term.write(spaces, min<int>(sizeof_array(spaces) - 1, i));
                i -= sizeof_array(spaces) - 1;
            }

            index += dx;
        }

        term.write("\n", 1);
    }

    if (m_row == total_rows)
        return state_none;

    static const char prompt[] = { "--More--" };
    term.write(prompt, sizeof_array(prompt) - 1);
    return state_pager;
}

//------------------------------------------------------------------------------
classic_match_ui::state classic_match_ui::query_prompt(
    unsigned char key,
    const context& context)
{
    switch(key)
    {
    case 'y':
    case 'Y':
    case ' ':
    case '\t':
    case '\r':
        return state_print_page;

    case 'n':
    case 'N':
    case 0x03: // ctrl-c
    case 0x04: // ctrl-d
    case 0x1b: // esc
        context.terminal.write("\n", 1);
        return state_none;
    }

    context.terminal.write("\x07", 1);
    return state_query;
}

//------------------------------------------------------------------------------
classic_match_ui::state classic_match_ui::pager_prompt(
    unsigned char key,
    const context& context)
{
    switch (key)
    {
    case ' ':
    case '\t':
        return state_print_page;

    case '\r':
        return state_print_one;

    case 'q':
    case 'Q':
    case 0x03: // ctrl-c
    case 0x04: // ctrl-d
    case 0x1b: // esc
        context.terminal.write("\n", 1);
        return state_none;
    }

    context.terminal.write("\x07", 1);
    return state_pager;
}



//------------------------------------------------------------------------------
class line_editor_2
{
public:
    struct desc
    {
        const char*     prompt; // MODE4
        const char*     command_delims; // MODE4
        const char*     quote_pair;
        const char*     word_delims;
        const char*     partial_delims;
        terminal*       terminal;
        editor_backend* backend;
        line_buffer*    buffer;
    };

                        line_editor_2(const desc& desc);
    bool                add_backend(editor_backend& backend);
    bool                add_generator(match_generator& generator);
    bool                get_line(char* out, int out_size);
    bool                edit(char* out, int out_size);
    bool                update();

private:
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

//------------------------------------------------------------------------------
line_editor_2::line_editor_2(const desc& desc)
: m_desc(desc)
, m_initialised(false)
, m_begun(false)
, m_prev_key(~0u)
{
    add_backend(*m_desc.backend);
    m_binder.set_default_backend(*m_desc.backend);
}

//------------------------------------------------------------------------------
void line_editor_2::initialise()
{
    if (m_initialised)
        return;

    for (auto backend : m_backends)
        backend->bind(m_binder);

    m_initialised = true;
}

//------------------------------------------------------------------------------
void line_editor_2::begin_line()
{
    m_begun = true;

    m_bind_resolver.reset();
    m_keys_size = 0;

    match_pipeline pipeline(m_matches);
    pipeline.reset();

    m_desc.terminal->begin();

    editor_backend::context context = {
        *m_desc.terminal,
        *m_desc.buffer,
        { m_words, m_desc.buffer->get_buffer() },
        m_matches,
    };

    for (auto backend : m_backends)
        backend->begin_line(m_desc.prompt, context);

    //m_desc.buffer->redraw();
}

//------------------------------------------------------------------------------
void line_editor_2::end_line()
{
    for (auto backend : m_backends)
        backend->end_line();

    m_desc.terminal->end();

    m_begun = false;
}

//------------------------------------------------------------------------------
bool line_editor_2::add_backend(editor_backend& backend)
{
    editor_backend** slot = m_backends.push_back();
    return (slot != nullptr) ? *slot = &backend, true : false;
}

//------------------------------------------------------------------------------
bool line_editor_2::add_generator(match_generator& generator)
{
    match_generator** slot = m_generators.push_back();
    return (slot != nullptr) ? *slot = &generator, true : false;
}

//------------------------------------------------------------------------------
bool line_editor_2::get_line(char* out, int out_size)
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
bool line_editor_2::edit(char* out, int out_size)
{
    // Update first so the init state goes through.
    while (update())
        m_desc.terminal->select();

    return get_line(out, out_size);
}

//------------------------------------------------------------------------------
bool line_editor_2::update()
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
void line_editor_2::record_input(unsigned char key)
{
    if (m_keys_size < sizeof_array(m_keys) - 1)
        m_keys[m_keys_size++] = key;
}

//------------------------------------------------------------------------------
void line_editor_2::dispatch()
{
    if (!m_bind_resolver.is_resolved())
        return;

    editor_backend::context context = {
        *m_desc.terminal,
        *m_desc.buffer,
        { m_words, m_desc.buffer->get_buffer() },
        m_matches,
    };

    m_keys[m_keys_size] = '\0';

    int id = m_bind_resolver.get_id();

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
void line_editor_2::collect_words()
{
    const char* line_buffer = m_desc.buffer->get_buffer();
    const int line_cursor = m_desc.buffer->get_cursor();

    m_words.clear();

    str_iter token_iter(line_buffer, line_cursor);
    str_tokeniser tokens(token_iter, m_desc.word_delims);
    tokens.add_quote_pair(m_desc.quote_pair);
    while (1)
    {
        const char* start = nullptr;
        int length = 0;
        if (!tokens.next(start, length))
            break;

        // Add the word.
        m_words.push_back();
        *(m_words.back()) = { short(start - line_buffer), length };

        // Find the best-fit delimiter.
        /* MODE4
        const char* best_delim = word_delims;
        const char* c = m_words
        while (c > line_buffer)
        {
            const char* delim = strchr(word_delims, *c);
            if (delim == nullptr)
                break;

            best_delim = max(delim, best_delim);
            --c;
        }
        word->delim = *best_delim;
        MODE4 */
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
void line_editor_2::accept_match(unsigned int index)
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

    // Clean the work tf it is a valid file system path.
    if (os::get_path_type(word.c_str()) != os::path_type_invalid)
        path::clean(word);

    buffer.remove(word_start, buffer.get_cursor());
    buffer.set_cursor(word_start);
    buffer.insert(word.c_str());

    // If this match doesn't make a new partial word, close it off
    int last_char = strlen(match) - 1;
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
void line_editor_2::update_internal()
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
        match_pipeline pipeline(m_matches);
        pipeline.reset();
        pipeline.generate({ m_words, m_desc.buffer->get_buffer() }, m_generators);
    }

    next_key.cursor_pos = m_desc.buffer->get_cursor();
    prev_key.value = m_prev_key;

    // Should we sort and select matches?
    if (next_key.value != prev_key.value)
    {
        str<64> needle;
        int needle_start = end_word.offset + end_word.length;
        const char* line = m_desc.buffer->get_buffer();
        needle.concat(line + needle_start, next_key.cursor_pos - needle_start);

        match_pipeline pipeline(m_matches);
        pipeline.select("normal", needle.c_str());
        pipeline.sort("alpha");

        m_prev_key = next_key.value;

        // Tell all the backends that the matches changed.
        editor_backend::context context = {
            *m_desc.terminal,
            *m_desc.buffer,
            { m_words, m_desc.buffer->get_buffer() },
            m_matches,
        };

        for (auto backend : m_backends)
            backend->on_matches_changed(context);
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    win_terminal terminal;

    classic_match_ui ui;
    rl_backend backend;

    line_editor_2::desc desc = {};
    desc.prompt = "testbed -> ";
    desc.quote_pair = "\"";
    desc.word_delims = " \t=";
    desc.partial_delims = "\\/:";
    desc.terminal = &terminal;
    desc.backend = &backend;
    desc.buffer = &backend;
    line_editor_2 editor(desc);
    editor.add_backend(ui);
    editor.add_generator(file_match_generator());

    char out[64];
    while (editor.edit(out, sizeof_array(out)));

    return 0;
}
