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

#include "clink_pch.h"
#include "clink.h"

//------------------------------------------------------------------------------
void            get_config_dir(char*, int);
void            str_cat(char*, const char*, int);
void            initialise_lua();
char**          lua_generate_matches(const char*, int, int);
void            move_cursor(int, int);
void            clear_to_eol();

int             g_match_palette[3]      = { -1, -1, -1 };
extern int      rl_visible_stats;
extern char*    _rl_term_forward_char;
extern char*    _rl_term_clreol;
extern int      _rl_screenwidth;
extern int      _rl_screenheight;
extern int      rl_display_fixed;
static int      g_new_history_count     = 0;
static char*    g_to_reedit             = NULL;

//------------------------------------------------------------------------------
static const char* g_header = 
    "clink v" CLINK_VERSION " : Enhancements for cmd.exe\n"
    "\n"
    "Copyright (c) 2012 Martin Ridgers\n"
    "Copyright (c) 1994-2012 Lua.org, PUC-Rio\n"
    "Copyright (c) 1987-2010 Free Software Foundation, Inc.\n"
    "\n"
    ;

//------------------------------------------------------------------------------
// This ensures the cursor is visible as printing to the console usually makes
// the cursor disappear momentarily.
static void display()
{
    rl_redisplay();
    move_cursor(0, 0);
}

//------------------------------------------------------------------------------
static int getc_impl(FILE* stream)
{
    int i;
    while (1)
    {
        wchar_t wc[2];
        char utf8[4];

        i = _getwch();
        if (i == 0)
        {
            i = 0xe0;
        }

        // treat esc like cmd.exe does - clear the line.
        if (i == 0x1b)
        {
            using_history();
            rl_delete_text(0, rl_end);
            rl_point = 0;
            display();
            continue;
        }

        if (i < 0x7f || i == 0xe0)
        {
            break;
        }

        // Convert to utf-8 and insert directly into rl's line buffer.
        wc[0] = (wchar_t)i;
        wc[1] = L'\0';

        WideCharToMultiByte(
            CP_UTF8, 0,
            wc, -1,
            utf8, sizeof(utf8),
            NULL,
            NULL
        );

        rl_insert_text(utf8);
        display();
    }

    // Set the "meta" key bit if the ALT key is pressed.
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
    {
        i |= 0x80;
    }

    return i;
}

//------------------------------------------------------------------------------
static int postprocess_matches(char** matches)
{
    char** m;
    int need_quote;

    // Convert forward slashes to backward ones and while we're at it, check
    // for spaces.
    need_quote = 0;
    m = matches;
    while (*m)
    {
        char* c = *m++;
        while (*c)
        {
            need_quote |= (*c == ' ');
            *c = (*c == '/') ? '\\' : *c;
            ++c;
        }
    }

#if 0
    // Do we need to prepend a quote?
    if (need_quote)
    {
        char* c = malloc(strlen(matches[0]) + 2);
        strcpy(c + 1, matches[0]);
        free(matches[0]);

        c[0] = '\"';
        matches[0] = c;
    }
#endif

    return 0;
}

//------------------------------------------------------------------------------
static int completion_shim(int count, int invoking_key)
{
    int ret;

    // rl_complete checks if it was called previously.
    if (rl_last_func == completion_shim)
    {
        rl_last_func = rl_complete;
    }

    rl_begin_undo_group();
    ret = rl_complete(count, invoking_key);

    // readline's path completion may have appended a '/'. If so; flip it.
    if ((rl_point > 0) && (rl_line_buffer[rl_point - 1] == '/'))
    {
        rl_delete_text(rl_point - 1, rl_point);
        --rl_point;
        rl_insert_text("\\");
    }

    rl_end_undo_group();
    return ret;
}

//------------------------------------------------------------------------------
static char** alternative_matches(const char* text, int start, int end)
{
    char* c;
    char** lua_matches;

    // Try the lua match generators first
    lua_matches = lua_generate_matches(text, start, end);
    if (lua_matches != NULL)
    {
        return lua_matches;
    }

    // We're going to use readline's path completion, which only works with
    // forward slashes. So, we slightly modify the completion state here.
    c = (char*)text;
    while (*c)
    {
        *c = (*c == '\\') ? '/' : *c;
        ++c;
    }

    return NULL;
}

//------------------------------------------------------------------------------
static void update_screen_size()
{
    HANDLE handle;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle != INVALID_HANDLE_VALUE)
    {
        if (GetConsoleScreenBufferInfo(handle, &csbi))
        {
            _rl_screenwidth = csbi.srWindow.Right - csbi.srWindow.Left;
            _rl_screenheight = csbi.srWindow.Bottom - csbi.srWindow.Top;
        }
    }
}

//------------------------------------------------------------------------------
static void display_matches(char** matches, int match_count, int max_length)
{
    int i;
    char** new_matches;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    WORD text_attrib;
    HANDLE std_out_handle;
    wchar_t buffer[512];
    int show_matches = 2;

    // The matches need to be processed so needless path information is removed
    // (this is caused by the \ and / hurdles).
    max_length = 0;
    ++match_count;
    new_matches = (char**)calloc(1, match_count * sizeof(char**));
    for (i = 0; i < match_count; ++i)
    {
        int len;
        char* slash = strrchr(matches[i], '\\');
        if (slash == NULL)
        {
            slash = strrchr(matches[i], ':');
        }

        new_matches[i] = (slash != NULL) ? slash + 1 : matches[i];

        len = (int)strlen(new_matches[i]);
        max_length = len > max_length ? len : max_length;
    }
    --match_count;

    std_out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(std_out_handle, &csbi);

    // Get the console's current colour settings
    if (g_match_palette[0] == -1)
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
        text_attrib |= (g_match_palette[0] & 0x0f);
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
        WriteConsoleW(std_out_handle, buffer, wcslen(buffer), &written, NULL);

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
            case 0x7f:
                show_matches = 0;
                break;
            }
        }
    }

    // Get readline to display the matches. Flush stream so we catch set colours
    update_screen_size();
    if (show_matches > 0)
    {
        rl_display_match_list(new_matches, match_count, max_length);
    }
    else
    {
        rl_crlf();
    }
    fflush(rl_outstream);

    // Reset console colour back to normal.
    SetConsoleTextAttribute(std_out_handle, csbi.wAttributes);
    rl_forced_update_display();
    rl_display_fixed = 1;

    free(new_matches);
}

//------------------------------------------------------------------------------
static void get_history_file_name(char* buffer, int size)
{
    get_config_dir(buffer, size);
    if (buffer[0])
    {
        str_cat(buffer, "/.history", size);
    }
}

//------------------------------------------------------------------------------
static void load_history()
{
    char buffer[1024];
    get_history_file_name(buffer, sizeof(buffer));
    read_history(buffer);
    using_history();
}

//------------------------------------------------------------------------------
void save_history()
{
    static const int max_history = 200;
    char buffer[1024];

    get_history_file_name(buffer, sizeof(buffer));

    // Write new history to the file, and truncate to our maximum.
    if (append_history(g_new_history_count, buffer) != 0)
    {
        write_history(buffer);
    }
    history_truncate_file(buffer, max_history);
}

//------------------------------------------------------------------------------
static int clear_line(int count, int invoking_key)
{
    using_history();
    rl_delete_text(0, rl_end);
    rl_point = 0;
    return 0;
}

//------------------------------------------------------------------------------
static int paste_from_clipboard(int count, int invoking_key)
{
    if (OpenClipboard(NULL) != FALSE)
    {
        HANDLE clip_data = GetClipboardData(CF_UNICODETEXT);
        if (clip_data != NULL)
        {
            wchar_t* from_clipboard = (wchar_t*)clip_data;
            char utf8[1024];

            WideCharToMultiByte(
                CP_UTF8, 0,
                from_clipboard, -1,
                utf8, sizeof(utf8),
                NULL, NULL
            );
            utf8[sizeof(utf8) - 1] = '\0';

            rl_insert_text(utf8);
        }

        CloseClipboard();
    }
    
    return 0;
}

//------------------------------------------------------------------------------
static int initialise_hook()
{
    // This is a bit of a hack. Ideally we should take care of this in
    // the termcap functions.
    _rl_term_forward_char = "\013";
    _rl_term_clreol = "\001";

    rl_redisplay_function = display;
    rl_getc_function = getc_impl;

    rl_completer_quote_characters = "\"";
    rl_ignore_some_completions_function = postprocess_matches;
    rl_basic_word_break_characters = " <>|";
    rl_completer_word_break_characters = " <>|";
    rl_completion_display_matches_hook = display_matches;
    rl_attempted_completion_function = alternative_matches;

    rl_add_funmap_entry("clink-completion-shim", completion_shim);
    rl_add_funmap_entry("clear-line", clear_line);
    rl_add_funmap_entry("paste-from-clipboard", paste_from_clipboard);

    initialise_lua();
    load_history();
    rl_re_read_init_file(0, 0);
    rl_visible_stats = 0;               // serves no purpose under win32.

    // Header.
    clear_to_eol();
    hooked_fprintf(NULL, "%s", g_header);
    
    rl_startup_hook = NULL;
    return 0;
}

//------------------------------------------------------------------------------
static int reedit_hook()
{
    rl_insert_text(g_to_reedit);

    free(g_to_reedit);
    g_to_reedit = NULL;
    
    rl_startup_hook = NULL;
    return 0;
}

//------------------------------------------------------------------------------
static void add_to_history(const char* line)
{
    const char* c;
    HIST_ENTRY* hist_entry;

    // Skip leading whitespace
    c = line;
    while (*c)
    {
        if (!isspace(*c))
        {
            break;
        }

        ++c;
    }

    // Skip empty lines
    if (*c == '\0')
    {
        return;
    }

    // Check the line's not a duplicate of the last in the history.
    using_history();
    hist_entry = previous_history();
    if (hist_entry != NULL)
    {
        if (strcmp(hist_entry->line, c) == 0)
        {
            return;
        }
    }

    // All's well. Add the line.
    using_history();
    add_history(line);

    ++g_new_history_count;
}

//------------------------------------------------------------------------------
void CLINK_API call_readline(
    const wchar_t* prompt,
    wchar_t* result,
    unsigned size
)
{
    static int initialised = 0;
    unsigned text_size;
    int expand_result;
    char* text;
    char* expanded;
    char prompt_utf8[1024];

    // Convery prompt to utf-8.
    WideCharToMultiByte(
        CP_UTF8, 0,
        prompt, -1,
        prompt_utf8, sizeof(prompt_utf8),
        NULL,
        NULL
    );

    // Initialisation
    if (!initialised)
    {
        rl_startup_hook = initialise_hook;
        initialised = 1;
    }

    // Call readline
    do
    {
        hooked_fprintf(NULL, "\r");
        text = readline(prompt_utf8);

        // Expand history designators in returned buffer.
        expanded = NULL;
        expand_result = history_expand(text, &expanded);
        if (expand_result < 0)
        {
            free(expanded);
        }
        else
        {
            free(text);
            text = expanded;

            // If there was some expansion then display the expanded result.
            if (expand_result > 0)
            {
                hooked_fprintf(NULL, "%s\n", text);
            }
        }

        add_to_history(text);
    }
    while (expand_result == 2);

    text_size = MultiByteToWideChar(CP_UTF8, 0, text, -1, result, 0);
    text_size = (size < text_size) ? size : strlen(text);
    text_size = MultiByteToWideChar(CP_UTF8, 0, text, -1, result, size);
    if (text_size + 2 <= size)
    {
        --text_size;
        result[text_size + 0] = L'\x0d';
        result[text_size + 1] = L'\x0a';
        result[text_size + 2] = L'\0';
    }
    else if (text_size + 1 <= size)
    {
        result[text_size] = L'\0';
    }

    free(text);
}
