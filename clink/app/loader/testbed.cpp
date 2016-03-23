#include "pch.h"
#include "rl/rl_line_editor.h"

#include <core/array.h>
#include <core/base.h>
#include <core/str_tokeniser.h>
#include <file_match_generator.h>
#include <line_state.h>
#include <matches/column_printer.h>
#include <matches/matches.h>
#include <terminal/ecma48_terminal.h>

static void end_line(char* line)
{
    puts("done!");
}

int testbed(int, char**)
{
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
        for (int j = end_word->length - 1; j >= 0; --j)
        {
            int c = line_buffer[end_word->offset + j];
            if (strchr(partial_delims, c) != nullptr)
            {
                end_word->partial = j;
                break;
            }
        }

        // SPAM!
        int j = 0;
        for (auto word : words)
            printf("%02d:%02d,%02d,%02d,%02x ", j++, word.offset, word.partial, word.length, word.delim);
        puts("");
        // SPAM!

#error partial only applies to the last word

        // Should we generate new matches?
        unsigned int next_match_key = int(end_word->offset) << 20;
        next_match_key |= int(end_word->partial & 0x3ff) << 10;
        if ((match_key & ~0x3ff) != next_match_key)
        {
            // MODE4
            auto& system = line_editor->get_match_system();
            line_state_2 ls = {words, line_buffer, line_cursor};
            system.generate_matches(ls, result);

            printf("Gen'd: %d\n", result.get_match_count());
        }

        // Should we sort and select matches?
        next_match_key |= int(end_word->length) & 0x3ff;
        printf("%08x > %08x\n", next_match_key, match_key);
        if (match_key != next_match_key)
        {
            match_key = next_match_key;
            puts("Selort!");
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
