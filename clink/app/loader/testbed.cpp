#include "pch.h"
#include "rl/rl_line_editor.h"

#include <core/array.h>
#include <core/base.h>
#include <core/str_tokeniser.h>
#include <terminal/ecma48_terminal.h>
#include <matches/column_printer.h>
#include <file_match_generator.h>

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

    for (int i = 0; ; ++i)
    {
        // Get line state from backend.
        const char* line_buffer = rl_line_buffer;
        const int line_cursor = rl_point;

        // Collect words.
        struct word {
            unsigned short offset;
            unsigned short length;
            unsigned short partial;
            unsigned short delim;
        };
        fixed_array<word, 2> words;

        int word_count = 0;

        str_iter token_iter(line_buffer, line_cursor);
        str_tokeniser tokens(token_iter, word_delims);
        const char* word_start = nullptr;
        int word_length = 0;
        while (tokens.next(word_start, word_length))
        {
            word* word = words.push_back();
            if (word == nullptr)
                word = words.back();

            *word = { short(word_start - line_buffer), word_length };

            // Find the best-fit delimiter.
            /* MODE4
            const char* best_delim = word_delims;
            const char* c = word_start - 1;
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

        // Add an empty word if the cursor at the beginning of one.
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

        // Should we generate new matches?
        unsigned int next_match_key = end_word->offset << 20;
        next_match_key |= (end_word->partial & 0x3ff) << 10;
        if ((match_key & ~0x3ff) != next_match_key)
        {
            puts("Generate!");
        }

        // Should we sort and select matches?
        next_match_key |= end_word->length & 0x3ff;
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
    terminal->end();

    return 0;
}
