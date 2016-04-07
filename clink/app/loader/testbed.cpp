#include "pch.h"
#include "rl/rl_line_editor.h"

#include <core/array.h>
#include <core/base.h>
#include <core/singleton.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <line_state.h>
#include <matches/column_printer.h>
#include <matches/match_pipeline.h>
#include <matches/matches.h>
#include <terminal/win_terminal.h>

void draw_matches(const matches&);

//------------------------------------------------------------------------------
class match_ui
{
public:
    virtual void update(bool matches_changed, const matches& result, terminal* terminal) = 0;
};

//------------------------------------------------------------------------------
class classic_match_ui
    : public match_ui
{
public:
    virtual void update(bool matches_changed, const matches& result, terminal* terminal) override
    {
        if (m_waiting & !matches_changed)
        {
            column_printer p(terminal);
            p.print(result);
            return;
        }

        m_waiting = true;
    }

private:
    bool m_waiting = false;
};

//------------------------------------------------------------------------------
class line_editor_backend
{
public:
    enum status
    {
        status_more_input,
        status_continue,
        status_done,
    };

    virtual void        begin() = 0;
    virtual status      update() = 0;
    virtual void        end() = 0;
    virtual int         get_cursor_pos() const = 0;
    virtual const char* get_buffer() const = 0;
};



//------------------------------------------------------------------------------
class rl_backend
    : public line_editor_backend
    , public singleton<rl_backend>
{
public:
    virtual void begin() override
    {
        auto handler = [] (char* line) { rl_backend::get()->done(line); };
        rl_callback_handler_install("testbed $ ", handler);

        m_done = false;
        m_eof = false;
    }

    virtual status update() override
    {
        rl_callback_read_char();

        int rl_state = rl_readline_state;
        rl_state &= ~RL_STATE_CALLBACK;
        rl_state &= ~RL_STATE_INITIALIZED;
        rl_state &= ~RL_STATE_OVERWRITE;
        rl_state &= ~RL_STATE_VICMDONCE;

        if (m_done)
            return status_done;

        return (rl_state ? status_more_input : status_continue);
    }

    virtual void end() override
    {
        rl_callback_handler_remove();
    }

    virtual int get_cursor_pos() const override
    {
        return rl_point;
    }

    virtual const char* get_buffer() const override
    {
        return (m_eof ? nullptr : rl_line_buffer);
    }

private:
    void done(const char* line)
    {
        m_done = true;
        m_eof = (line == nullptr);
    }

    bool m_done = false;
    bool m_eof = false;
};



//------------------------------------------------------------------------------
class line_editor_2
{
public:
    typedef line_editor_backend backend;

    struct desc
    {
        const char* quote_char;
        const char* word_delims;
        const char* partial_delims;
        terminal*   terminal;
        backend*    backend;
    };

                    line_editor_2(const desc& desc);
    match_system&   get_match_system();
    bool            get_line(str_base& out);
    bool            edit(str_base& out);
    bool            update();

private:
    enum
    {
        state_init,
        state_update,
        state_backend_input,
        state_done,
    };

    void            collect_words(array_base<word>& words) const;
    void            update_init();
    void            update_internal();
    void            update_backend();
    void            update_done();
    desc            m_desc;
    match_system    m_match_system;
    matches         m_matches;
    unsigned int    m_match_key;
    int             m_state;
};

//------------------------------------------------------------------------------
line_editor_2::line_editor_2(const desc& desc)
: m_match_key(~0u)
, m_state(state_init)
, m_desc(desc)
{
}

//------------------------------------------------------------------------------
match_system& line_editor_2::get_match_system()
{
    return m_match_system;
}

//------------------------------------------------------------------------------
bool line_editor_2::get_line(str_base& out)
{
    if (m_state != state_done)
        return false;

    if (const char* line = m_desc.backend->get_buffer())
    {
        out.copy(line);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
bool line_editor_2::edit(str_base& out)
{
    if (m_state != state_init)
        return false;

    // Update first so the init state goes through.
    while (update())
        m_desc.terminal->select();

    return get_line(out);
}

//------------------------------------------------------------------------------
bool line_editor_2::update()
{
    if (m_state == state_init)
    {
        update_init();
        update_internal();
        return true;
    }

    update_backend();
    if (m_state == state_update)
    {
        update_internal();
        return true;
    }

    if (m_state != state_done)
        return true;

    update_done();
    return false;
}

//------------------------------------------------------------------------------
void line_editor_2::collect_words(array_base<word>& words) const
{
    const char* line_buffer = m_desc.backend->get_buffer();
    const int line_cursor = m_desc.backend->get_cursor_pos();

    str_iter token_iter(line_buffer, line_cursor);
    str_tokeniser tokens(token_iter, m_desc.word_delims);
    tokens.add_quote_pair(m_desc.quote_char);
    while (1)
    {
        const char* start = nullptr;
        int length = 0;
        if (!tokens.next(start, length))
            break;

        // Add the word.
        words.push_back();
        *(words.back()) = { short(start - line_buffer), length };

        // Find the best-fit delimiter.
        /* MODE4
        const char* best_delim = word_delims;
        const char* c = words
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
    word* end_word = words.back();
    if (!end_word || end_word->offset + end_word->length < line_cursor)
    {
        words.push_back();
        *(words.back()) = { line_cursor };
    }

    // Adjust for quotes.
    for (word& word : words)
    {
        const char* start = line_buffer + word.offset;

        int start_quoted = (start[0] == m_desc.quote_char[0]);
        int end_quoted = 0;
        if (word.length > 1)
            end_quoted = (start[word.length - 1] == m_desc.quote_char[0]);

        word.offset += start_quoted;
        word.length -= start_quoted + end_quoted;
    }

    // Adjust the completing word for partiality.
    end_word = words.back();
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

    // MODE4
    int j = 0;
    puts("");
    for (auto word : words)
        printf("%02d:%02d,%02d ", j++, word.offset, word.length);
    puts("");
    // MODE4
}

//------------------------------------------------------------------------------
void line_editor_2::update_init()
{
    m_desc.terminal->begin();
    m_desc.backend->begin();

    m_state = state_update;
}

//------------------------------------------------------------------------------
void line_editor_2::update_internal()
{
    // Collect words.
    fixed_array<word, 128> words;
    collect_words(words);

    // Should we generate new matches?
    const word* end_word = words.back();
    unsigned int next_match_key = int(end_word->offset) << 20;
    next_match_key |= (end_word->length & 0x3ff) << 10;
    if ((m_match_key & ~0x3ff) != next_match_key)
    {
        match_pipeline pipeline(m_match_system, m_matches);
        pipeline.generate({ words, m_desc.backend->get_buffer() });

        /* MODE4 */ printf("generate: %d\n", m_matches.get_match_count());
    }

    const int cursor = m_desc.backend->get_cursor_pos();
    next_match_key |= int(cursor) & 0x3ff;
    bool matches_changed = (m_match_key != next_match_key);

    // Should we sort and select matches?
    if (matches_changed)
    {
        str<64> needle;
        int needle_start = end_word->offset + end_word->length;
        const char* line = m_desc.backend->get_buffer();
        needle.concat(line + needle_start, cursor - needle_start);

        match_pipeline pipeline(m_match_system, m_matches);
        pipeline.select("normal", needle.c_str());
        pipeline.sort("alpha");

        /* MODE4 */ printf("select & sort: '%s'\n", needle.c_str());
    }

    static classic_match_ui ui; // MODE4
    ui.update(matches_changed, m_matches, m_desc.terminal);

    /* MODE4 */ draw_matches(m_matches);
    /* MODE4 */ rl_forced_update_display();
    m_match_key = next_match_key;
    m_state = state_backend_input;
}

//------------------------------------------------------------------------------
void line_editor_2::update_backend()
{
    switch (m_desc.backend->update())
    {
    case backend::status_continue:  m_state = state_update; break;
    case backend::status_done:      m_state = state_done;   break;
    }
}

//------------------------------------------------------------------------------
void line_editor_2::update_done()
{
    m_desc.backend->end();
    m_desc.terminal->end();

    m_state = state_done;
}



//------------------------------------------------------------------------------
void draw_matches(const matches& result)
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(handle, &csbi);

    COORD cur = { csbi.srWindow.Left, csbi.srWindow.Top };
    SetConsoleCursorPosition(handle, cur);
    SetConsoleTextAttribute(handle, 0x70);

    for (int i = 0, n = result.get_match_count(); i < 35; ++i)
    {
        const char* match = "";
        if (i < n)
            match = result.get_match(i);

        printf("%02d : %48s\n", i, match);
    }

    SetConsoleTextAttribute(handle, csbi.wAttributes);
}

int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    // MODE4
    win_terminal terminal;
    column_printer printer(&terminal);
    line_editor::desc _desc = { "testbed", &terminal, &printer };
    auto* line_editor = create_rl_line_editor(_desc);
    // MODE4

    rl_backend backend;

    line_editor_2::desc desc = {};
    desc.quote_char = "\"";
    desc.word_delims = " \t=";
    desc.partial_delims = "\\/:";
    desc.terminal = &terminal;
    desc.backend = &backend;
    line_editor_2 editor(desc);

    match_system& system = editor.get_match_system();
    system.add_generator(0, file_match_generator());
    system.add_selector("normal", normal_match_selector());
    system.add_sorter("alpha", alpha_match_sorter());

    str<> out;
    editor.edit(out);

    return 0;
}
