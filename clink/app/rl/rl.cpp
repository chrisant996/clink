// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/str.h>
#include <settings/settings.h>

extern "C" {
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
char**              lua_match_display_filter(char**, int);
void                load_history();
void                save_history();
void                add_to_history(const char*);
int                 expand_from_history(const char*, char**);

int                 g_slash_translation = 0;
extern setting_bool g_history_io;

extern "C" {
extern int          rl_display_fixed;
extern int          rl_editing_mode;
extern const char*  rl_filename_quote_characters;
extern int          _rl_complete_mark_directories;
int                 rl_complete(int, int);
int                 rl_menu_complete(int, int);
int                 rl_backward_menu_complete(int, int);
} // extern "C"

//------------------------------------------------------------------------------
char** match_display_filter(char** matches, int match_count)
{
#if MODE4
    int i;
    char** new_matches;

    ++match_count;

    // First, see if there's a Lua function registered to filter matches for
    // display (this is set via clink.match_display_filter).
    new_matches = lua_match_display_filter(matches, match_count);
    if (new_matches != nullptr)
    {
        return new_matches;
    }

    // The matches need to be processed so needless path information is removed
    // (this is caused by the \ and / hurdles).
    new_matches = (char**)calloc(1, match_count * sizeof(char*));
    for (i = 0; i < match_count; ++i)
    {
        int is_dir = 0;
        int len;
        char* base = nullptr;

        // If matches are files then strip off the path and establish if they
        // are directories.
        if (rl_filename_completion_desired)
        {
            DWORD file_attrib;

            base = strrchr(matches[i], '\\');
            if (base == nullptr)
            {
                base = strrchr(matches[i], ':');
            }

            // Is this a dir?
            file_attrib = GetFileAttributes(matches[i]);
            if (file_attrib != INVALID_FILE_ATTRIBUTES)
            {
                is_dir = !!(file_attrib & FILE_ATTRIBUTE_DIRECTORY);
            }
        }
        base = (base == nullptr) ? matches[i] : base + 1;
        len = (int)strlen(base) + is_dir;

        new_matches[i] = (char*)malloc(len + 1);
        strcpy(new_matches[i], base);
        if (is_dir)
        {
            strcat(new_matches[i], "\\");
        }
    }

    return new_matches;
#else
    return nullptr;
#endif // MODE4
}

//------------------------------------------------------------------------------
bool call_readline(const char* prompt, str_base& out)
{
    // Make sure that EOL wrap is on. Readline's told the terminal supports it.
    int stdout_flags = ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT;
    HANDLE handle_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(handle_stdout, stdout_flags);

    char* text;
    int expand_result;
    do
    {
        // Call readline
        text = readline(prompt);
        if (text == nullptr)
            return false;

        // Expand history designators in returned buffer.
        char* expanded = nullptr;
        expand_result = expand_from_history(text, &expanded);
        if (expand_result > 0 && expanded != nullptr)
        {
            free(text);
            text = expanded;
        }

        // Should we read the history from disk.
        if (g_history_io.get())
        {
            load_history();
            add_to_history(text);
            save_history();
        }
        else
            add_to_history(text);
    }
    while (!text || expand_result == 2);

    out = text;
    free(text);
    return true;
}
