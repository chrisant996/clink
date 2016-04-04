#include "pch.h"
#include "rl/rl_line_editor.h"

#include <core/array.h>
#include <core/base.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <line_state.h>
#include <matches/column_printer.h>
#include <matches/match_pipeline.h>
#include <matches/matches.h>
#include <terminal/ecma48_terminal.h>

void draw_matches(const matches&);

//------------------------------------------------------------------------------
class line_editor_2
{
public:
    enum result
    {
        result_more_input,
        result_eot,
        result_done,
    };

    struct desc
    {
        const char* word_delims;
        const char* partial_delims;
        terminal*   terminal;
    };

                    line_editor_2(const desc& desc);
    bool            get_line(str_base& out);
    bool            edit(str_base& out);
    result          update();

private:
    enum
    {
        state_init,
        state_input,
        state_backend_input,
        state_done,
    };

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
bool line_editor_2::get_line(str_base& out)
{
    if (m_state != state_done)
        return false;

    return out.copy(rl_line_buffer);
}

//------------------------------------------------------------------------------
bool line_editor_2::edit(str_base& out)
{
    if (m_state != state_init)
        return false;

    while (m_state != state_done)
        update();

    return get_line(out);
}

//------------------------------------------------------------------------------
line_editor_2::result line_editor_2::update()
{
    switch (m_state)
    {
    case state_init:
        update_init();
        /* fall through */

    case state_input:
        update_internal();
        break;

    case state_backend_input:
        update_backend();
        break;
    }

    return result_done;
}

//------------------------------------------------------------------------------
void line_editor_2::update_init()
{
    m_desc.terminal->begin();
    auto handler = [] (char* line) { };
    rl_callback_handler_install("testbed $ ", handler);

    m_state = state_input;
}

//------------------------------------------------------------------------------
void line_editor_2::update_internal()
{
    // Get line state from backend. MODE4
    const char* line_buffer = rl_line_buffer;
    const int line_cursor = rl_point;

    // Collect words.
    fixed_array<word, 128> words;

    str_iter token_iter(line_buffer, line_cursor);
    str_tokeniser tokens(token_iter, m_desc.word_delims);
    while (1)
    {
        const char* start = nullptr;
        int length = 0;
        if (!tokens.next(start, length))
            break;

        word* word = words.push_back();
        if (word == nullptr)
            word = words.back();

        *word = { short(start - line_buffer), length };

        // Find the best-fit delimiter.
        /* MODE4
           const char* best_delim = word_delims;
           const char* c = start - 1;
           while (c > line_buffer)
           {
           const char* delim = strchr(word_delims, *c);
           if (delim == nullptr)
           break;

           best_delim = max(delim, best_delim);
           --c;
           }
           word->delim = *best_delim;
         */
    }

    // Add an empty word if the cursor is at the beginning of one.
    word* end_word = words.back();
    if (end_word == nullptr ||
            end_word->offset + end_word->length < line_cursor)
    {
        words.push_back();
        *(words.back()) = { line_cursor };
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

    // SPAM!
    int j = 0;
    for (auto word : words)
        printf("%02d:%02d,%02d ", j++, word.offset, word.length);
    puts("");
    // SPAM!

    // Should we generate new matches?
    unsigned int next_match_key = int(end_word->offset) << 20;
    next_match_key |= (partial & 0x3ff) << 10;
    if ((m_match_key & ~0x3ff) != next_match_key)
    {
        line_state state = { words, line_buffer };

        match_pipeline pipeline(m_match_system, m_matches);
        pipeline.generate(state);

        printf("generate: %d\n", m_matches.get_match_count());
    }

    // Should we sort and select matches?
    next_match_key |= int(line_cursor) & 0x3ff;
    printf("%08x > %08x\n", next_match_key, m_match_key);
    if (m_match_key != next_match_key)
    {
        m_match_key = next_match_key;

        str<64> needle;
        int needle_start = end_word->offset + end_word->length;
        needle.concat(line_buffer + needle_start, line_cursor - needle_start);

        match_pipeline pipeline(m_match_system, m_matches);
        pipeline.select("normal", needle.c_str());
        pipeline.sort("alpha");

        printf("select & sort: '%s'\n", needle.c_str());
        draw_matches(m_matches);
    }
}

//------------------------------------------------------------------------------
void line_editor_2::update_backend()
{
    rl_forced_update_display(); // MODE4
    rl_callback_read_char();

    int rl_state = rl_readline_state;
    rl_state &= ~RL_STATE_CALLBACK;
    rl_state &= ~RL_STATE_INITIALIZED;
    rl_state &= ~RL_STATE_OVERWRITE;
    rl_state &= ~RL_STATE_VICMDONCE;
    m_state = rl_state ? state_backend_input : state_input;
}

//------------------------------------------------------------------------------
void line_editor_2::update_done()
{
    rl_callback_handler_remove();
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
    SetConsoleCursorPosition(handle, csbi.dwCursorPosition);
}

int testbed(int, char**)
{
    str_compare_scope _(str_compare_scope::relaxed);

    terminal* terminal = new ecma48_terminal();
    match_printer* printer = new column_printer(terminal);
    line_editor::desc desc = { "testbed", terminal, printer };
    auto* line_editor = create_rl_line_editor(desc);

    match_system& system = line_editor->get_match_system();
    system.add_generator(0, file_match_generator());
    system.add_selector("normal", normal_match_selector());
    system.add_sorter("alpha", alpha_match_sorter());

    static const char* word_delims = " \t";
    static const char* partial_delims = "\\/:";

    unsigned int match_key = ~0;
    matches result;

    line_editor_2::desc d = {};
    d.word_delims = " \t";
    d.partial_delims = "\\/:";
    d.terminal = terminal;
    line_editor_2 editor(d);
    str<> out;
    editor.edit(out);

    return 0;
}
