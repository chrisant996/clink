/* Copyright (c) 2012 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "shared/util.h"

//------------------------------------------------------------------------------
char**              lua_match_display_filter(char**, int);
int                 get_clink_setting_int(const char*);
void                get_config_dir(char*, int);
char*               filter_prompt(const char*);
static int          completion_shim_impl(int, int, int (*)(int, int));
void                load_history();
void                save_history();
void                add_to_history(const char*);
int                 expand_from_history(const char*, char**);

int                 g_slash_translation = 0;

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
static void translate_matches(char** matches, char from, char to)
{
    char** m;

    if (!rl_filename_completion_desired)
    {
        return;
    }

    // Convert forward slashes to back slashes
    m = matches;
    while (*m)
    {
        char* c = *m;
        while (*c)
        {
            *c = (*c == from) ? to : *c;
            ++c;
        }

        ++m;
    }
}

//------------------------------------------------------------------------------
static void quote_matches(char** matches)
{
    // The least disruptive way to inject quotes into the command line is do it
    // at the last possible moment. Either the first match (the lcd) needs a
    // quote, or the next character the user may type needs quoting

    char** m;
    int need_quote;
    int lcd_length;
    int lcd_needs_quote;

    // Does the lcd have a quote char? Readline will add the quote if it thinks
    // it's completing file names.
    lcd_needs_quote = strpbrk(matches[0], rl_filename_quote_characters) != nullptr;
    if (lcd_needs_quote && (rl_filename_completion_desired != 0))
    {
        return;
    }

    if (rl_completion_quote_character == '\"')
    {
        return;
    }

    if (rl_completion_suppress_quote)
    {
        return;
    }

    // Check other matches for characters that need quoting.
    need_quote = lcd_needs_quote;
    lcd_length = (int)strlen(matches[0]);
    m = matches + 1;
    while (*m && !need_quote)
    {
        int i;

        i = strlen(*m);
        if (i > lcd_length)
        {
            int c = *(*m + lcd_length);
            need_quote = strchr(rl_filename_quote_characters, c) != nullptr;
        }

        ++m;
    }

    // So... do we need to prepend a quote?
    if (need_quote)
    {
        char* c = (char*)malloc(strlen(matches[0]) + 8);
        strcpy(c + 1, matches[0]);
        free(matches[0]);

        c[0] = '\"';
        matches[0] = c;

        // If there's a single match then there should be a closing quote.
        if (m - matches == 1)
        {
            strcat(c, "\"");
        }
    }
}

//------------------------------------------------------------------------------
int postprocess_matches(char** matches)
{
    char** m;
    int need_quote;
    int first_needs_quotes;

    if (g_slash_translation >= 0)
    {
        switch (g_slash_translation)
        {
        case 1:     translate_matches(matches, '\\', '/');  break;
        default:    translate_matches(matches, '/', '\\');  break;
        }
    }

    quote_matches(matches);

    return 0;
}

//------------------------------------------------------------------------------
static void suffix_translation()
{
    // readline's path completion may have appended a '/'. If so; flip it.

    char from;
    char* to;

    if (!rl_filename_completion_desired)
    {
        return;
    }

    if (g_slash_translation < 0)
    {
        return;
    }

    // Decide what direction we're going in.
    switch (g_slash_translation)
    {
    case 1:     from = '\\'; to = "/";  break;
    default:    from = '/';  to = "\\"; break;
    }

    // Swap the trailing slash, using Readline's API to maintain undo state.
    if ((rl_point > 0) && (rl_line_buffer[rl_point - 1] == from))
    {
        rl_delete_text(rl_point - 1, rl_point);
        --rl_point;
        rl_insert_text(to);
    }
}

//------------------------------------------------------------------------------
int completion_shim(int count, int invoking_key)
{
    return completion_shim_impl(count, invoking_key, rl_complete);
}

//------------------------------------------------------------------------------
int menu_completion_shim(int count, int invoking_key)
{
    return completion_shim_impl(count, invoking_key, rl_menu_complete);
}

//------------------------------------------------------------------------------
int backward_menu_completion_shim(int count, int invoking_key)
{
    return completion_shim_impl(count, invoking_key, rl_backward_menu_complete);
}

//------------------------------------------------------------------------------
static int completion_shim_impl(int count, int invoking_key, int (*rl_func)(int, int))
{
    int ret;

    // rl complete functions checks if it was called previously, so restore it.
    if (rl_last_func == completion_shim ||
        rl_last_func == menu_completion_shim ||
        rl_last_func == backward_menu_completion_shim)
    {
        rl_last_func = rl_func;
    }

    rl_begin_undo_group();
    ret = rl_func(count, invoking_key);
    suffix_translation();
    rl_end_undo_group();

    g_slash_translation = 0;

    return ret;
}

//------------------------------------------------------------------------------
char** match_display_filter(char** matches, int match_count)
{
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
}

//------------------------------------------------------------------------------
void display_matches(char** matches, int match_count, int longest)
{
    int i;
    char** new_matches;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    WORD text_attrib;
    HANDLE std_out_handle;
    wchar_t buffer[512];
    int show_matches = 2;
    int match_colour;

    // Process matches and recalculate the longest match length.
    new_matches = match_display_filter(matches, match_count);

    longest = 0;
    for (i = 0; i < (match_count + 1); ++i)
    {
        int len = (int)strlen(new_matches[i]);
        longest = (len > longest) ? len : longest;
    }

    std_out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(std_out_handle, &csbi);

    // Get the console's current colour settings
    match_colour = get_clink_setting_int("match_colour");
    if (match_colour == -1)
    {
        // Pick a suitable foreground colour, check fg isn't the same as bg, and set.
        text_attrib = csbi.wAttributes;
        text_attrib ^= 0x08;

        if ((text_attrib & 0xf0) == (text_attrib & 0x0f))
        {
            text_attrib ^= FOREGROUND_INTENSITY;
        }
    }
    else
    {
        text_attrib = csbi.wAttributes & 0xf0;
        text_attrib |= (match_colour & 0x0f);
    }

    SetConsoleTextAttribute(std_out_handle, text_attrib);

    // If there's lots of matches, check with the user before displaying them
    // This matches readline's behaviour, which will get skipped (annoyingly)
    if ((rl_completion_query_items > 0) &&
        (match_count >= rl_completion_query_items))
    {
        DWORD written;

        _snwprintf(
            buffer,
            sizeof_array(buffer),
            L"\nDisplay all %d possibilities? (y or n)",
            match_count
        );
        WriteConsoleW(std_out_handle, buffer, wcslen(buffer), &written, nullptr);

        while (show_matches > 1)
        {
            int c = rl_read_key();
            switch (c)
            {
            case 'y':
            case 'Y':
            case ' ':
                show_matches = 1;
                break;

            case 'n':
            case 'N':
            case 0x03: // handling Ctrl+C
            case 0x7f:
                show_matches = 0;
                break;
            }
        }
    }

    // Get readline to display the matches.
    if (show_matches > 0)
    {
        // Turn of '/' suffix for directories. RL assumes '/', which isn't the
        // case, plus clink uses colours instead.
        int j = _rl_complete_mark_directories;
        _rl_complete_mark_directories = 0;

        rl_display_match_list(new_matches, match_count, longest);

        _rl_complete_mark_directories = j;
    }
    else
    {
        rl_crlf();
    }

    // Reset console colour back to normal.
    SetConsoleTextAttribute(std_out_handle, csbi.wAttributes);
    rl_forced_update_display();
    rl_display_fixed = 1;

    // Tidy up.
    for (i = 0; i < match_count; ++i)
    {
        free(new_matches[i]);
    }
    free(new_matches);
}

//------------------------------------------------------------------------------
static char* call_readline_impl(const char* prompt)
{
    int expand_result;
    char* text;
    char* expanded;
    char* prepared_prompt;

    // Make sure that EOL wrap is on. Readline's told the terminal supports it.
    {
        int stdout_flags = ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT;
        HANDLE handle_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleMode(handle_stdout, stdout_flags);
    }

    // Filter the prompt through Lua so scripts can modify it.
    prepared_prompt = filter_prompt(prompt);

    do
    {
        // Call readline
        rl_already_prompted = (prompt == nullptr);
        text = readline(prepared_prompt ? prepared_prompt : "");
        if (!text)
        {
            goto call_readline_epilogue;
        }

        // Expand history designators in returned buffer.
        expanded = nullptr;
        expand_result = expand_from_history(text, &expanded);
        if (expand_result > 0 && expanded != nullptr)
        {
            free(text);
            text = expanded;
        }

        // Should we read the history from disk.
        if (get_clink_setting_int("history_io"))
        {
            load_history();
            add_to_history(text);
            save_history();
        }
        else
            add_to_history(text);
    }
    while (!text || expand_result == 2);

call_readline_epilogue:
    free(prepared_prompt);
    return text;
}

//------------------------------------------------------------------------------
int call_readline_w(const wchar_t* prompt, wchar_t* result, unsigned size)
{
    unsigned text_size;
    char* text;
    char prompt_utf8[1024];

    // Convert prompt to utf-8.
    WideCharToMultiByte(CP_UTF8, 0, prompt, -1, prompt_utf8, sizeof(prompt_utf8),
        nullptr, nullptr);

    // Call readline.
    result[0] = L'\0';
    text = call_readline_impl(prompt_utf8);
    if (text == nullptr)
    {
        // EOF.
        return 1;
    }

    // Convert result back to wchar_t.
    text_size = MultiByteToWideChar(CP_UTF8, 0, text, -1, result, 0);
    text_size = (size < text_size) ? size : strlen(text);
    text_size = MultiByteToWideChar(CP_UTF8, 0, text, -1, result, size);
    result[size - 1] = L'\0';

    free(text);
    return 0;
}
