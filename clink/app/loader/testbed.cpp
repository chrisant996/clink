#include "pch.h"
#include "rl/rl_line_editor.h"

#include <core/array.h>
#include <core/base.h>
#include <core/str_compare.h>
#include <core/str_tokeniser.h>
#include <file_match_generator.h>
#include <line_state.h>
#include <matches/column_printer.h>
#include <matches/matches.h>
#include <terminal/ecma48_terminal.h>

#include <algorithm>

//------------------------------------------------------------------------------
class match_pipeline
{
public:
                    match_pipeline(match_system& system, matches& result);
    void            generate(line_state& state);
    void            select(const char* selector_name, const char* needle);
    void            sort(const char* sort_name);

private:
    match_system&   m_system;
    matches&        m_result;
};

//------------------------------------------------------------------------------
match_pipeline::match_pipeline(match_system& system, matches& result)
: m_system(system)
, m_result(result)
{
}

//------------------------------------------------------------------------------
void match_pipeline::generate(line_state& state)
{
    m_result.reset();
    for (const auto& iter : m_system.m_generators)
    {
        auto* generator = (match_generator*)(iter.ptr);
        if (generator->generate(state, m_result))
            break;
    }
}

//------------------------------------------------------------------------------
void match_pipeline::select(const char* selector_name, const char* needle)
{
    const matches::store& store = m_result.m_store;
    matches::info* infos = &(m_result.m_infos[0]);

    int count = m_result.get_match_count();
    if (!count)
        return;

    for (int i = 0; i < count; ++i)
    {
        const char* name = store.get(infos[i].store_id);
        int j = str_compare(needle, name);
        infos[i].selected = (j < 0 || !needle[j]);
    }

    m_result.coalesce();
}

//------------------------------------------------------------------------------
void match_pipeline::sort(const char* sorter_name)
{
    const matches::store& store = m_result.m_store;
    matches::info* infos = &(m_result.m_infos[0]);

    int count = m_result.get_match_count();
    if (!count)
        return;

    struct predicate
    {
        predicate(const matches::store& store) : store(store) {}

        bool operator () (const matches::info& lhs, const matches::info& rhs)
        {
            const char* l = store.get(lhs.store_id);
            const char* r = store.get(rhs.store_id);
            return (stricmp(l, r) < 0);
        }

        const matches::store& store;
    };

    std::sort(infos, infos + count, predicate(store));
}

//------------------------------------------------------------------------------
static void end_line(char* line) { puts("done!"); }

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

    file_match_generator file_generator;
    line_editor->get_match_system().add_generator(&file_generator, 0);

    terminal->begin();
    rl_callback_handler_install("testbed>", end_line);

    static const char* word_delims = " \t";
    static const char* partial_delims = "\\/";

    unsigned int match_key = ~0;
    matches result;

    for (int i = 0; ; ++i)
    {
        // Get line state from backend.
        const char* line_buffer = rl_line_buffer;
        const int line_cursor = rl_point;

        // Collect words.
        fixed_array<word, 128> words;

        str_iter token_iter(line_buffer, line_cursor);
        str_tokeniser tokens(token_iter, word_delims);
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
            if (strchr(partial_delims, c) == nullptr)
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
        if ((match_key & ~0x3ff) != next_match_key)
        {
            line_state state = { words, line_buffer };

            auto& system = line_editor->get_match_system();
            match_pipeline pipeline(system, result);
            pipeline.generate(state);

            printf("generate: %d\n", result.get_match_count());
        }

        // Should we sort and select matches?
        next_match_key |= int(line_cursor) & 0x3ff;
        printf("%08x > %08x\n", next_match_key, match_key);
        if (match_key != next_match_key)
        {
            match_key = next_match_key;

            str<64> needle;
            int needle_start = end_word->offset + end_word->length;
            needle.concat(line_buffer + needle_start, line_cursor - needle_start);

            auto& system = line_editor->get_match_system();
            match_pipeline pipeline(system, result);
            pipeline.select("normal", needle.c_str());
            pipeline.sort("alpha");

            printf("select & sort: '%s'\n", needle.c_str());
            draw_matches(result);
        }

        // Send to backend.
        rl_forced_update_display();
        while (1)
        {
            rl_callback_read_char();

            int rl_state = rl_readline_state;
            rl_state &= ~RL_STATE_CALLBACK;
            rl_state &= ~RL_STATE_INITIALIZED;
            rl_state &= ~RL_STATE_OVERWRITE;
            rl_state &= ~RL_STATE_VICMDONCE;
            if (!rl_state)
                break;
        }

        // SPAM!
        printf("\n\n%03d [%08x]\n", i, rl_readline_state);
        // SPAM!
    }

    rl_callback_handler_remove();
    terminal->end();

    return 0;
}
