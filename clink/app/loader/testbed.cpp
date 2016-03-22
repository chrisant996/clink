#include "pch.h"
#include "rl/rl_line_editor.h"

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

    match_system& match_system = line_editor->get_match_system();

    auto* file_generator = new file_match_generator();
    match_system.add_generator(file_generator, 0);

    terminal->begin();
    rl_callback_handler_install("testbed>", end_line);
    for (int i = 0; ; ++i)
    {
        // Should matches be updated?
        int word_count = 0;
        str_iter token_iter(rl_line_buffer, rl_point);
        str_tokeniser tokens(token_iter, " \t");
        const char* word_start;
        int word_length;
        while (tokens.next(word_start, word_length))
            ++word_count;

        if (!rl_point || strchr(" \t", rl_line_buffer[rl_point - 1]))
            ++word_count;

        printf("wc: %02d\n", word_count);

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

        printf("\n%03d [%08x]\n", i, rl_readline_state);
    }
    terminal->end();

    return 0;
}
