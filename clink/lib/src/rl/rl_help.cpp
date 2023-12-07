// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_unordered_set.h>
#include <core/debugheap.h>
#include <core/linear_allocator.h>
#include <terminal/printer.h>
#include <terminal/wcwidth.h>
#include <terminal/terminal.h>
#include <terminal/terminal_helpers.h>
#include <terminal/ecma48_wrapper.h>
#include "rl_commands.h"
#include "editor_module.h"
#include "pager.h"
#include "ellipsify.h"
#include "clink_ctrlevent.h"

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
#include <readline/rldefs.h>
extern int __complete_get_screenwidth(void);
}

#include <vector>
#include <algorithm>
#include <assert.h>

//------------------------------------------------------------------------------
extern pager* g_pager;
extern setting_bool g_terminal_raw_esc;

//------------------------------------------------------------------------------
static linear_allocator s_macro_name_store(4096);
static str_unordered_map<str_moveable> s_macro_descriptions;

//------------------------------------------------------------------------------
void clear_macro_descriptions()
{
    s_macro_descriptions.clear();
    s_macro_name_store.reset();
}

//------------------------------------------------------------------------------
void add_macro_description(const char* macro, const char* desc)
{
    dbg_ignore_scope(snapshot, "macro descriptions");

    const auto iter = s_macro_descriptions.find(macro);
    if (iter == s_macro_descriptions.end())
    {
        macro = s_macro_name_store.store(macro);
        if (!macro)
            return;
    }
    else
    {
        macro = iter->first;
    }

    s_macro_descriptions.emplace(macro, desc);
}

//------------------------------------------------------------------------------
const char* lookup_macro_description(const char* macro)
{
    str<> tmp;
    tmp << "\"" << macro << "\"";

    const auto iter = s_macro_descriptions.find(tmp.c_str());
    if (iter == s_macro_descriptions.end())
        return nullptr;
    return iter->second.c_str();
}

//------------------------------------------------------------------------------
struct Keyentry
{
    int32 cat;
    int32 sort;
    char* key_name;
    char* macro_text;
    const char* func_name;
    const char* func_desc;
    bool warning;
    bool prefix;
};

//------------------------------------------------------------------------------
struct Keydesc
{
    Keydesc(const char* name, int32 cat, const char* desc) : name(name), desc(desc), cat(cat) {}
    const char* name;   // command name
    const char* desc;   // command description
    int32 cat;          // command category
};

//------------------------------------------------------------------------------
typedef std::map<rl_command_func_t*, struct Keydesc> keydesc_map;
static keydesc_map* s_pmap_keydesc = nullptr;

//------------------------------------------------------------------------------
static const char* const c_headings[] =
{
    "Uncategorized",
    "Basic",
    "Cursor Movement",
    "Completion",
    "History",
    "Kill and Yank",
    "Selection",
    "Scrolling",
    "Miscellaneous",
    "Macros",
};
static_assert(sizeof_array(c_headings) == keycat_MAX, "c_headings must have the same number of entries as the keycat enum");

//------------------------------------------------------------------------------
static const struct {
    const char* name;
    rl_command_func_t* func;
    int32 cat;
    const char* desc;
} c_func_descriptions[] = {
  { "abort", rl_abort, keycat_basic, "Abort the current editing command and ring the terminal's bell (subject to the setting of 'bell-style')" },
  { "accept-line", rl_newline, keycat_basic, "Accept the input line.  The line may be added to the history list for future recall" },
/* begin_clink_change */
  { "add-history", rl_add_history, keycat_history, "Add the current line to the history without executing it, and clear the input line" },
/* end_clink_change */
  { "arrow-key-prefix", rl_arrow_keys, keycat_cursor, "" },
  { "backward-byte", rl_backward_byte, keycat_cursor, "Move back a single byte" },
  { "backward-char", rl_backward_char, keycat_cursor, "Move back a character" },
  { "backward-delete-char", rl_rubout, keycat_basic, "Delete the character behind the cursor point.  A numeric argument means to kill the characters instead of deleting them" },
  { "backward-kill-line", rl_backward_kill_line, keycat_killyank, "Kill backward from the cursor point to the beginning of the current line.  With a negative numeric argument, kills forward from the cursor to the end of the current line" },
  { "backward-kill-word", rl_backward_kill_word, keycat_basic, "Kill the word behind the cursor point.  Word boundaries are the same as 'backward-word'" },
  { "backward-word", rl_backward_word, keycat_cursor, "Move back to the start of the current or previous word" },
  { "beginning-of-history", rl_beginning_of_history, keycat_history, "Move to the first line in the history" },
  { "beginning-of-line", rl_beg_of_line, keycat_basic, "Move to the start of the current line" },
  { "bracketed-paste-begin", rl_bracketed_paste_begin, keycat_misc, "" },
  { "call-last-kbd-macro", rl_call_last_kbd_macro, keycat_misc, "Re-execute the last keyboard macro defined, by making the characters in the macro appear as if typed at the keyboard" },
  { "capitalize-word", rl_capitalize_word, keycat_misc, "Capitalize the current (or following) word.  With a negative argument, capitalizes the previous word, but does not move the cursor point" },
  { "character-search", rl_char_search, keycat_basic, "A character is read and the cursor point is moved to the next occurrence of that character.  A negative count searches for previous occurrences" },
  { "character-search-backward", rl_backward_char_search, keycat_basic, "A character is read and the cursor point is moved to the previous occurrence of that character.  A negative count searches for subsequent occurrences" },
  { "clear-display", rl_clear_display, keycat_misc, "Clear the terminal screen and the scrollback buffer (if possible), then redraw the current line, leaving the current line at the top of the screen" },
  { "clear-screen", rl_clear_screen, keycat_misc, "Clear the terminal screen, then redraw the current line, leaving the current line at the top of the screen" },
  { "complete", rl_complete, keycat_completion, "Perform completion on the text before the cursor point" },
  { "copy-backward-word", rl_copy_backward_word, keycat_killyank, "Copy the word before the cursor point to the kill buffer.  The word boundaries are the same as 'backward-word'" },
  { "copy-forward-word", rl_copy_forward_word, keycat_killyank, "Copy the word following the cursor point to the kill buffer.  The word boundaries are the same as 'forward-word'" },
  { "copy-region-as-kill", rl_copy_region_to_kill, keycat_killyank, "Copy the text in the marked region to the kill buffer, so it can be yanked right away" },
  { "delete-char", rl_delete, keycat_basic, "Delete the character at the cursor point" },
  { "delete-char-or-list", rl_delete_or_show_completions, keycat_basic, "Deletes the character at the cursor, or lists completions if at the end of the line" },
  { "delete-horizontal-space", rl_delete_horizontal_space, keycat_basic, "Delete all spaces and tabs around the cursor point" },
  { "digit-argument", rl_digit_argument, keycat_misc, "Start or accumulate a numeric argument to a command.  Alt+- starts a negative argument" },
  { "do-lowercase-version", rl_do_lowercase_version, keycat_misc, "If the metafied character X is upper case, run the command that is bound to the corresponding metafied lower case character.  The behavior is undefined if X is already lower case" },
  { "downcase-word", rl_downcase_word, keycat_misc, "Lowercase the current (or following) word.  With a negative argument, lowercases the previous word, but does not move the cursor point" },
  { "dump-functions", rl_dump_functions, keycat_misc, "Print all of the functions and their key bindings to the output stream.  If a numeric argument is supplied, formats the output so that it can be made part of an INPUTRC file" },
  { "dump-macros", rl_dump_macros, keycat_misc, "Print all of the key sequences bound to macros and the strings they output.  If a numeric argument is supplied, formats the output so that it can be made part of an INPUTRC file" },
  { "dump-variables", rl_dump_variables, keycat_misc, "Print all of the Readline variables and their values to the output stream.  If a numeric argument is supplied, formats the output so that it can be made part of an INPUTRC file" },
  { "emacs-editing-mode", rl_emacs_editing_mode, keycat_misc, "When in 'vi' command mode, this causes a switch to 'emacs' editing mode" },
  { "end-kbd-macro", rl_end_kbd_macro, keycat_misc, "Stop saving the characters typed into the current keyboard macro and save the definition" },
  { "end-of-history", rl_end_of_history, keycat_history, "Move to the end of the input history, i.e. the line currently being entered" },
  //{ "end-of-line", clink_end_of_line, keycat_basic, "Move to the end of the line, or insert suggestion" },
  { "exchange-point-and-mark", rl_exchange_point_and_mark, keycat_misc, "Swap the cursor point with the mark.  Sets the current cursor position to the saved position, and saves the old cursor position as the mark" },
  { "fetch-history", rl_fetch_history, keycat_history, "With a numeric argument, fetch that entry from the history list and make it the current line.  Without an argument, move back to the first entry in the history list" },
  { "forward-backward-delete-char", rl_rubout_or_delete, keycat_basic, "Delete the character at the cursor point, unless the cursor is at the end of the line, in which case the character behind the cursor is deleted" },
  //{ "forward-byte", clink_forward_byte, keycat_cursor, "Move forward a single byte, or insert suggestion" },
  //{ "forward-char", clink_forward_char, keycat_cursor, "Move forward a character, or insert suggestion" },
  { "forward-search-history", rl_forward_search_history, keycat_history, "Incremental search forward starting at the current line and moving 'down' through the history as necessary.  Sets the marked region to the matched text" },
  //{ "forward-word", clink_forward_word, keycat_cursor, "Move forward to the end of the next word, or insert next suggested word" },
  { "history-search-backward", rl_history_search_backward, keycat_history, "Search backward through the history for the string of characters between the start of the current line and the cursor point.  The search string must match at the beginning of a history line.  This is a non-incremental search" },
  { "history-search-forward", rl_history_search_forward, keycat_history, "Search forward through the history for the string of characters between the start of the current line and the cursor point.  The search string must match at the beginning of a history line.  This is a non-incremental search" },
  { "history-substring-search-backward", rl_history_substr_search_backward, keycat_history, "Search backward through the history for the string of characters between the start of the current line and the cursor point.  The search string may match anywhere in a history line.  This is a non-incremental search" },
  { "history-substring-search-forward", rl_history_substr_search_forward, keycat_history, "Search forward through the history for the string of characters between the start of the current line and the cursor point.  The search string may match anywhere in a history line.  This is a non-incremental search" },
  { "insert-close", rl_insert_close, keycat_misc, "Insert the typed closing character and briefly move the cursor to the matching opening character" },
  { "insert-comment", rl_insert_comment, keycat_misc, "Insert '::' at the beginning of the input line and accept the line" },
  { "insert-completions", rl_insert_completions, keycat_misc, "Insert all the completions that 'possible-completions' would list" },
  { "kill-whole-line", rl_kill_full_line, keycat_killyank, "Kill all characters on the current line, no matter where the cursor point is" },
  { "kill-line", rl_kill_line, keycat_killyank, "Kill the text from the cursor point to the end of the line.  With a negative numeric argument, kills backward from the cursor to the beginning of the current line" },
  { "kill-region", rl_kill_region, keycat_killyank, "Kill the text in the current marked region" },
  { "kill-word", rl_kill_word, keycat_basic, "Kill from the cursor point to the end of the current word, or if between words, to the end of the next word.  Word boundaries are the same as 'forward-word'" },
  { "menu-complete", rl_menu_complete, keycat_completion, "Replace the completion word with the common prefix.  Repeated execution steps through the possible completions" },
  { "menu-complete-backward", rl_backward_menu_complete, keycat_completion, "Like 'menu-complete' but in reverse" },
  { "next-history", rl_get_next_history, keycat_history, "Move 'forward' through the history list, fetching the next command" },
  { "next-screen-line", rl_next_screen_line, keycat_cursor, "Attempt to move the cursor point to the same screen column on the next screen line" },
  { "non-incremental-forward-search-history", rl_noninc_forward_search, keycat_history, "Search forward starting at the current line and moving 'down' through the history as necessary using a non-incremental search for a string supplied by the user.  The search string may match anywhere in a history line" },
  { "non-incremental-reverse-search-history", rl_noninc_reverse_search, keycat_history, "Search backward starting at the current line and moving 'up' through the history as necessary using a non-incremental search for a string supplied by the user.  The search string may match anywhere in a history line" },
  { "non-incremental-forward-search-history-again", rl_noninc_forward_search_again, keycat_history, "" },
  { "non-incremental-reverse-search-history-again", rl_noninc_reverse_search_again, keycat_history, "" },
  { "old-menu-complete", rl_old_menu_complete, keycat_completion, "Replace the completion word with the next match.  Repeated execution steps through the possible completions" },
/* begin_clink_change */
  { "old-menu-complete-backward", rl_backward_old_menu_complete, keycat_completion, "Like 'old-menu-complete' but in reverse" },
/* end_clink_change */
  { "operate-and-get-next", rl_operate_and_get_next, keycat_history, "Accept the current line, and fetch the next line relative to the current line from the history for editing.  A numeric argument, if supplied, specifies the history entry to use instead of the current line" },
  { "overwrite-mode", rl_overwrite_mode, keycat_basic, "Toggle overwrite mode.  This commands affects only 'emacs' mode.  Each input line always starts in insert mode" },
#if defined (_WIN32)
  //{ "paste-from-clipboard", rl_paste_from_clipboard, keycat_basic, "" },
#endif
  { "possible-completions", rl_possible_completions, keycat_completion, "List the possible completions of the text before the cursor point" },
  { "previous-history", rl_get_previous_history, keycat_history, "Move 'back' through the history list, fetching the previous command" },
  { "previous-screen-line", rl_previous_screen_line, keycat_basic, "Attempt to move the cursor point to the same screen column on the previous screen line" },
  { "print-last-kbd-macro", rl_print_last_kbd_macro, keycat_misc, "Print the last keboard macro defined in a format suitable for the INPUTRC file" },
  { "quoted-insert", rl_quoted_insert, keycat_basic, "Add the next character typed to the line verbatim" },
  { "re-read-init-file", rl_re_read_init_file, keycat_misc, "Read in the contents of the INPUTRC file, and incorporate any bindings or variable assignments found there" },
  { "redraw-current-line", rl_refresh_line, keycat_misc, "Refresh the current line" },
/* begin_clink_change */
  { "remove-history", rl_remove_history, keycat_history, "While searching history, removes the current line from the history" },
/* end_clink_change */
  { "reverse-search-history", rl_reverse_search_history, keycat_history, "Incremental search backward starting at the current line and moving 'up' through the history as necessary.  Sets the marked region to the matched text" },
  { "revert-line", rl_revert_line, keycat_basic, "Undo all changes made to this line.  This is like executing the 'undo' command enough times to get back to the beginning" },
  //{ "self-insert", rl_insert },
  { "set-mark", rl_set_mark, keycat_misc, "Set the mark to the cursor point.  If a numeric argument is supplied, sets the mark to that position" },
  { "skip-csi-sequence", rl_skip_csi_sequence, keycat_misc, "" },
  { "start-kbd-macro", rl_start_kbd_macro, keycat_misc, "Begin saving the characters typed into the current keyboard macro" },
  { "tab-insert", rl_tab_insert, keycat_basic, "Insert a tab character" },
  { "tilde-expand", rl_tilde_expand, keycat_completion, "Perform tilde expansion on the current word" },
  { "transpose-chars", rl_transpose_chars, keycat_basic, "Drag the character before the cursor point forward over the character at the cursor, moving the cursor forward as well.  If the cursor point is at the end of the line, then this transposes the last two characters of the line" },
  { "transpose-words", rl_transpose_words, keycat_basic, "Drag the word before the cursor point past the word after the cursor, moving the cursor past that word as well.  If the cursor point is at the end of the line, this transposes the last two words on the line" },
  { "tty-status", rl_tty_status, keycat_misc, "" },
  { "undo", rl_undo_command, keycat_basic, "Incremental undo, separately remembered for each line" },
  { "universal-argument", rl_universal_argument, keycat_misc, "Multiply numeric argument by 4 and enter argument input mode" },
  { "unix-filename-rubout", rl_unix_filename_rubout, keycat_killyank, "Kill the word behind the cursor point, using white space and the path separator as the word boundaries.  The killed text is saved on the kill-ring" },
  { "unix-line-discard", rl_unix_line_discard, keycat_killyank, "Kill backward from the cursor point to the beginning of the current line" },
  { "unix-word-rubout", rl_unix_word_rubout, keycat_killyank, "Kill the word behind the cursor point, using white space as a word boundary.  The killed text is saved on the kill-ring" },
  { "upcase-word", rl_upcase_word, keycat_misc, "Uppercase the current (or following) word.  With a negative argument, uppercases the previous word, but does not move the cursor point" },
  { "yank", rl_yank, keycat_killyank, "Yank the top of the kill ring into the buffer at the cursor point" },
  { "yank-last-arg", rl_yank_last_arg, keycat_history, "Insert last argument from the previous history entry.  With a numeric argument, behaves exactly like 'yank-nth-arg'.  Repeated execution moves back through the history list, inserting the last word (or nth word) of each line in turn" },
  { "yank-nth-arg", rl_yank_nth_arg, keycat_history, "Insert the first argument from the previous history entry (e.g. second word on the line).  With an argument N, inserts the Nth word from the previous history entry (0 refers to the first word).  A negative argument inserts the Nth word from the end of the history entry.  The argument is extracted as if the '!N' history expansion had been specified" },
  { "yank-pop", rl_yank_pop, keycat_killyank, "Rotate the kill-ring and yank the new top; but only if the prior command is 'yank' or 'yank-pop'" },

#if defined (VI_MODE)
  { "vi-append-eol", rl_vi_append_eol, keycat_misc, "Append after end of line" },
  { "vi-append-mode", rl_vi_append_mode, keycat_misc, "Append after the cursor point" },
  { "vi-arg-digit", rl_vi_arg_digit, keycat_misc, "Start or accumulate a numeric argument to a command, or 0 by itself moves to beginning of line" },
  { "vi-back-to-indent", rl_vi_back_to_indent, keycat_cursor, "Move to first non-space character in the current line" },
  { "vi-backward-bigword", rl_vi_bWord, keycat_cursor, "Move back to the start of the current or previous space delimited word" },
  { "vi-backward-word", rl_vi_bword, keycat_cursor, "Move back to the start of the current or previous word" },
  //{ "vi-bWord", rl_vi_bWord },	/* BEWARE: name matching is case insensitive */
  //{ "vi-bword", rl_vi_bword },	/* BEWARE: name matching is case insensitive */
  { "vi-change-case", rl_vi_change_case, keycat_misc, "Change the case of the character at the cursor point and move forward to the next character" },
  { "vi-change-char", rl_vi_change_char, keycat_misc, "Replace the character at the cursor point with the next character typed" },
  { "vi-change-to", rl_vi_change_to, keycat_misc, "Delete from the cursor point to the next movement command, and enter insert mode" },
  { "vi-char-search", rl_vi_char_search, keycat_misc, "" },
  { "vi-column", rl_vi_column, keycat_cursor, "Move to the column specified by the numeric argument" },
  { "vi-complete", rl_vi_complete, keycat_completion, "" },
  { "vi-delete", rl_vi_delete, keycat_basic, "Delete the character at the cursor point" },
  { "vi-delete-to", rl_vi_delete_to, keycat_misc, "Delete from the cursor point to the next movement command" },
  //{ "vi-eWord", rl_vi_eWord },
  { "vi-editing-mode", rl_vi_editing_mode, keycat_misc, "When in 'emacs' editing mode, this causes a switch to 'vi' editing mode" },
  { "vi-end-bigword", rl_vi_eWord, keycat_cursor, "Move forward to the end of the next space delimited word" },
  { "vi-end-word", rl_vi_end_word, keycat_cursor, "Move forward to the end of the next word, or next space delimited word if bound to an uppercase key" },
  { "vi-eof-maybe", rl_vi_eof_maybe, keycat_misc, "" },
  { "vi-eword", rl_vi_eword, keycat_cursor, "Move forward to the end of the next word" },
  //{ "vi-fWord", rl_vi_fWord },	/* BEWARE: name matching is case insensitive */
  { "vi-fetch-history", rl_vi_fetch_history, keycat_history, "Move to the history entry matching the numeric argument, or to the beginning of the history" },
  { "vi-first-print", rl_vi_first_print, keycat_cursor, "Move to first non-space character in the current line" },
  { "vi-forward-bigword", rl_vi_fWord, keycat_cursor, "Move forward to the beginning of the next space delimited word" },
  { "vi-forward-word", rl_vi_fword, keycat_cursor, "Move forward to the beginning of the next word" },
  //{ "vi-fword", rl_vi_fword },	/* BEWARE: name matching is case insensitive */
  { "vi-goto-mark", rl_vi_goto_mark, keycat_misc, "" },
  { "vi-insert-beg", rl_vi_insert_beg, keycat_basic, "Move to the beginning of the line, and enter insert mode" },
  { "vi-insertion-mode", rl_vi_insert_mode, keycat_basic, "Enter insert mode" },
  { "vi-match", rl_vi_match, keycat_misc, "Match brackets" },
  { "vi-movement-mode", rl_vi_movement_mode, keycat_basic, "Enter command mode (movement mode)" },
  { "vi-next-word", rl_vi_next_word, keycat_cursor, "Move forward to the beginning of the next word, or next space delimited word if bound to an uppercase key" },
  { "vi-overstrike", rl_vi_overstrike, keycat_misc, "Overwrites the character at the cursor point with the character this is bound to" },
  { "vi-overstrike-delete", rl_vi_overstrike_delete, keycat_misc, "" },
  { "vi-prev-word", rl_vi_prev_word, keycat_cursor, "Move back to the start of the current or previous word, or space delimited word if bound to an uppercase key" },
  { "vi-put", rl_vi_put, keycat_killyank, "" },
  { "vi-redo", rl_vi_redo, keycat_misc, "" },
  { "vi-replace", rl_vi_replace, keycat_misc, "Enter replace mode, and ESC to enter command (movement) mode" },
  { "vi-rubout", rl_vi_rubout, keycat_misc, "" },
  { "vi-search", rl_vi_search, keycat_misc, "Perform a vi style search, if bound to '/' or '?'" },
  { "vi-search-again", rl_vi_search_again, keycat_misc, "Search again for the last thing searched for, if bound to 'n' or 'N'" },
  { "vi-set-mark", rl_vi_set_mark, keycat_misc, "" },
  { "vi-subst", rl_vi_subst, keycat_misc, "" },
  { "vi-tilde-expand", rl_vi_tilde_expand, keycat_completion, "Perform tilde expansion on the current word, and enter insert mode" },
  { "vi-undo", rl_vi_undo, keycat_basic, "Incremental undo, separately remembered for each line" },
  { "vi-unix-word-rubout", rl_vi_unix_word_rubout, keycat_killyank, "" },
  { "vi-yank-arg", rl_vi_yank_arg, keycat_history, "Insert the last argument from the previous history entry.  With an argument N, inserts the Nth word from the previous history entry (1 refers to the first word).  A negative argument inserts the Nth word from the end of the history entry." },
  { "vi-yank-pop", rl_vi_yank_pop, keycat_killyank, "" },
  { "vi-yank-to", rl_vi_yank_to, keycat_killyank, "" },
#endif /* VI_MODE */
};

//------------------------------------------------------------------------------
static bool ensure_keydesc_map()
{
    static bool s_inited = false;
    if (!s_inited && funmap)
    {
        s_inited = true;

        dbg_ignore_scope(snapshot, "ensure_keydesc_map");

        if (!s_pmap_keydesc)
            s_pmap_keydesc = new keydesc_map;

        s_pmap_keydesc->emplace(rl_insert_close, std::move(Keydesc("insert-close", 0, nullptr)));

        FUNMAP** funcs = funmap;
        while (*funcs != nullptr)
        {
            FUNMAP* func = *funcs;

            auto const& iter = s_pmap_keydesc->find(func->function);
            if (iter == s_pmap_keydesc->end())
                s_pmap_keydesc->emplace(func->function, std::move(Keydesc(func->name, 0, nullptr)));
            else if (!iter->second.name) // Don't overwrite existing name; works around case sensitivity bug with some VI mode commands.
                iter->second.name = func->name;

            ++funcs;
        }

        for (auto const& f : c_func_descriptions)
        {
            auto const& iter = s_pmap_keydesc->find(f.func);
            assert(iter != s_pmap_keydesc->end()); // Command no longer exists?
            if (iter != s_pmap_keydesc->end())
            {
                // Command should either not have a name yet, or the name must
                // match, or be a known exception.
#ifdef DEBUG
                if (iter->second.name && strcmp(iter->second.name, f.name))
                {
                    static const char* const c_overwritable[] =
                    {
                        "insert-last-argument",
                    };

                    bool keydesc_overwrite = true;
                    for (auto const& o : c_overwritable)
                    {
                        if (!strcmp(o, iter->second.name))
                        {
                            keydesc_overwrite = false;
                            break;
                        }
                    }

                    assert(!keydesc_overwrite);
                }
#endif
                iter->second.name = f.name;
                iter->second.cat = f.cat;
                iter->second.desc = f.desc;
            }
        }

#ifdef DEBUG
        for (auto const& i : *s_pmap_keydesc)
        {
            assert(i.second.name);
            // assert(i.second.cat);
            // assert(i.second.desc);
        }
#endif
    }
    return !!s_pmap_keydesc;
}

//------------------------------------------------------------------------------
void clink_add_funmap_entry(const char *name, rl_command_func_t *func, int32 cat, const char* desc)
{
    assert(name);
    assert(func);
    assert(desc);

    rl_add_funmap_entry(name, func);

    if (!s_pmap_keydesc)
        s_pmap_keydesc = new keydesc_map;

    auto const& iter = s_pmap_keydesc->find(func);
    if (iter == s_pmap_keydesc->end())
    {
        s_pmap_keydesc->emplace(func, std::move(Keydesc(name, cat, desc)));
    }
    else
    {
        // A command's info should not change.
        assert(!iter->second.name || !strcmp(iter->second.name, name));
        assert(!iter->second.cat || iter->second.cat == cat);
        assert(!iter->second.desc || !strcmp(iter->second.desc, desc));
        iter->second.name = name;
        iter->second.cat = cat;
        iter->second.desc = desc;
    }
}

//------------------------------------------------------------------------------
static const char* get_function_name(int32 (*func_addr)(int32, int32))
{
    auto const& iter = s_pmap_keydesc->find(func_addr);
    if (iter != s_pmap_keydesc->end())
        return iter->second.name;

    return nullptr;
}

//------------------------------------------------------------------------------
static bool get_function_info(int32 (*func_addr)(int32, int32), const char** desc, int32* cat)
{
    auto const& iter = s_pmap_keydesc->find(func_addr);
    if (iter != s_pmap_keydesc->end())
    {
        if (desc) *desc = iter->second.desc;
        if (cat) *cat = iter->second.cat;
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
static void concat_key_string(int32 i, str<32>& keyseq)
{
    assert(i >= 0);
    assert(i < 256);

    char c = uint8(i);
    keyseq.concat_no_truncate(&c, 1);
}

//------------------------------------------------------------------------------
static bool translate_keyseq(const char* keyseq, uint32 len, char** key_name, bool friendly, int32& sort)
{
    static const char ctrl_map[] = "@abcdefghijklmnopqrstuvwxyz[\\]^_";

    str<> tmp;
    int32 order = 0;
    sort = 0;

    const bool raw_esc = g_terminal_raw_esc.get();

    // TODO: Produce identical sort order for both friend names and raw names?

    bool first_key = true;
    if (!friendly)
    {
        tmp << "\"";

        uint32 comma_threshold = 0;
        for (uint32 i = 0; i < len; i++)
        {
            if (!i && len == 2 && keyseq[0] == 0x1b && keyseq[1] != 0x1b)
            {
                comma_threshold++;
                tmp << "\\M-";
                if (first_key)
                    sort |= 4;
                continue;
            }

            char key = keyseq[i];

            if (key == 0x1b)
            {
                tmp << "\\e";
                if (first_key)
                    sort |= 4;
                continue;
            }

            if (key >= 0 && keyseq[i] < ' ')
            {
                tmp << "\\C-";
                tmp.concat(&ctrl_map[key], 1);
                if (first_key)
                    sort |= 2;
                first_key = false;
                continue;
            }

            if (key == RUBOUT)
            {
                tmp << "\\C-?";
                if (first_key)
                    sort |= 2;
                first_key = false;
                continue;
            }

            if (key == '\\' || key == '"')
                tmp << "\\";
            tmp.concat(&key, 1);
            first_key = false;
        }

        tmp << "\"";

        sort <<= 16;
    }
    else
    {
        int32 need_comma = 0;
        const char* keyseq_end = keyseq + len;
        while (keyseq < keyseq_end)
        {
            int32 keyseq_len;
            int32 eqclass = 0;
            const char* keyname = find_key_name(keyseq, keyseq_len, eqclass, order);
            if (keyname)
            {
                if (need_comma > 0)
                    tmp.concat(",", 1);
                tmp.concat(keyname);
                need_comma = 1;
                keyseq += keyseq_len;
            }
            else
            {
                if (*keyseq == '\x1b' && keyseq_end - keyseq >= 2)
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;
                    if (!raw_esc || keyseq[1] != '\x1b')
                    {
                        tmp.concat("A-");
                        eqclass |= 4;
                        keyseq++;
                        if (*keyseq >= 'A' && *keyseq <= 'Z')
                        {
                            tmp.concat("S-");
                            eqclass |= 1;
                        }
                    }
                }
                if (*keyseq >= 0 && *keyseq < ' ')
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    tmp.concat("C-", 2);
                    tmp.concat(&ctrl_map[uint8(*keyseq)], 1);
                    eqclass |= 2;
                    need_comma = 1;
                    keyseq++;
                }
                else
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;

                    if (uint8(*keyseq) == 0x7f)
                    {
                        tmp.concat("C-Bkspc");
                        eqclass |= 2;
                    }
                    else
                    {
                        tmp.concat(keyseq, 1);
                    }
                    keyseq++;
                }
            }

            if (first_key)
            {
                sort = (eqclass << 16) + (order & 0xffff);
                first_key = false;
            }
        }
    }

    if (!tmp.length())
    {
        *key_name = nullptr;
        return false;
    }

    *key_name = (char*)malloc(tmp.length() + 1);
    if (!*key_name)
        return false;

    memcpy(*key_name, tmp.c_str(), tmp.length() + 1);
    return true;
}

//------------------------------------------------------------------------------
static bool maybe_exclude_function(rl_command_func_t func)
{
    static rl_command_func_t* const exclude[] = {
        rl_insert,
        rl_do_lowercase_version,
        rl_bracketed_paste_begin,
        nullptr
    };

    for (rl_command_func_t* const* walk = exclude; *walk; ++walk)
    {
        if (*walk == func)
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------
static Keyentry* collect_keymap(
    Keymap map,
    Keyentry* collector,
    int32* offset,
    int32* max,
    str<32>& keyseq,
    bool friendly,
    bool categories,
    std::vector<str_moveable>* warnings)
{
    int32 i;

    if (!ensure_keydesc_map())
        return nullptr;

    for (i = 0; i < KEYMAP_SIZE; ++i)
    {
        const bool prefix = (i == ANYOTHERKEY);
        const KEYMAP_ENTRY& entry = map[i];
        if (entry.function == nullptr)
            continue;

        // Recursively chain to another keymap.
        if (entry.type == ISKMAP)
        {
            uint32 old_len = keyseq.length();
            concat_key_string(i, keyseq);
            collector = collect_keymap((Keymap)entry.function, collector, offset, max, keyseq, friendly, categories, warnings);
            keyseq.truncate(old_len);
            continue;
        }

        // Add entry for a function or macro.
        if (entry.type == ISFUNC && maybe_exclude_function(entry.function))
            continue;

        int32 cat = keycat_macros;
        const char *name = nullptr;
        const char *desc = nullptr;
        char *macro = nullptr;
        if (entry.type == ISFUNC)
        {
            name = get_function_name(entry.function);
            if (name == nullptr)
                continue;
            get_function_info(entry.function, &desc, &cat);
        }

        uint32 old_len = keyseq.length();
        if (!prefix)
            concat_key_string(i, keyseq);

        if (*offset >= *max)
        {
            *max *= 2;
            collector = (Keyentry *)realloc(collector, sizeof(collector[0]) * *max);
        }

        int32 sort;
        if (translate_keyseq(keyseq.c_str(), keyseq.length(), &collector[*offset].key_name, friendly, sort))
        {
            Keyentry& out = collector[*offset];
            out.sort = sort;
            if (entry.type == ISMACR)
            {
                out.macro_text = _rl_untranslate_macro_value((char *)entry.function, 0);
                desc = lookup_macro_description(out.macro_text);
            }
            else
                out.macro_text = nullptr;
            out.warning = false;
            out.prefix = prefix;

            if (friendly && warnings && keyseq.length() > 2)
            {
                const char* k = keyseq.c_str();
                if ((k[0] == 'A' || k[0] == 'M' || k[0] == 'C') && (k[1] == '-'))
                {
                    str_moveable s;
                    bool second = (k[2] == 'A' || k[2] == 'M' || k[2] == 'C') && (k[3] == '-');
                    char actual1[4] = { k[0], k[1] };
                    char actual2[4] = { k[2], k[3] };
                    char intent1[4] = { '\\', k[0] == 'A' ? 'M' : k[0], k[1] };
                    char intent2[4] = { '\\', k[2] == 'A' ? 'M' : k[2], k[3] };
                    s << "\x1b[1mwarning:\x1b[m key \x1b[7m" << out.key_name << "\x1b[m looks like a typo; did you mean \"" << intent1;
                    if (second)
                        s << intent2;
                    s << "\" instead of \"" << actual1;
                    if (second)
                        s << actual2;
                    s << "\"?";
                    warnings->push_back(std::move(s));
                    out.warning = true;
                }
            }

            out.func_name = name;
            out.func_desc = desc;
            out.cat = categories ? cat : 0;
            ++(*offset);
        }

        keyseq.truncate(old_len);
    }

    return collector;
}

//------------------------------------------------------------------------------
struct filter_keymap { rl_command_func_t* command; const char* macro; };
struct command_binding { str_moveable key; int32 sort; };
static void collect_command_bindings(Keymap map, const filter_keymap& filter, bool friendly, str<32>& keyseq, std::vector<command_binding>& bindings)
{
    for (int32 i = 0; i < KEYMAP_SIZE; ++i)
    {
        const bool prefix = (i == ANYOTHERKEY);
        const KEYMAP_ENTRY& entry = map[i];
        if (entry.function == nullptr)
            continue;

        // Recursively chain to another keymap.
        if (entry.type == ISKMAP)
        {
            uint32 old_len = keyseq.length();
            concat_key_string(i, keyseq);
            collect_command_bindings((Keymap)entry.function, filter, friendly, keyseq, bindings);
            keyseq.truncate(old_len);
            continue;
        }

        if (entry.type == ISFUNC)
        {
            if (!filter.command || filter.command != entry.function)
                continue;
        }
        else if (entry.type == ISMACR)
        {
            if (!filter.macro)
                continue;
            char* macro_text = _rl_untranslate_macro_value((char *)entry.function, 0);
            const bool skip = (strcmp(macro_text, filter.macro) != 0);
            free(macro_text);
            if (skip)
                continue;
        }
        else
        {
            assert(false);
            continue;
        }

        uint32 old_len = keyseq.length();
        if (!prefix)
            concat_key_string(i, keyseq);

        int32 sort;
        char* key_name = nullptr;
        if (translate_keyseq(keyseq.c_str(), keyseq.length(), &key_name, friendly, sort))
        {
            command_binding binding;
            binding.key = key_name;
            binding.sort = sort;
            bindings.emplace_back(std::move(binding));
        }
        free(key_name);

        keyseq.truncate(old_len);
    }
}

//------------------------------------------------------------------------------
static Keyentry* collect_functions(
    Keyentry* collector,
    int32* offset,
    int32* max,
    bool categories)
{
    if (!funmap)
        return nullptr;

    str_unordered_set seen_name;
    std::unordered_set<rl_command_func_t*> seen_func;
    for (int32 i = 1; i < *offset; i++)
    {
        const Keyentry& e = collector[i];
        const char* name = e.func_name;
        if (name)
            seen_name.emplace(name);
    }

#if defined (VI_MODE)
    const bool is_vi_mode = (rl_editing_mode == vi_mode);
#endif

    for (const FUNMAP* const* walk = funmap; *walk; ++walk)
    {
        rl_command_func_t* func = (*walk)->function;
        if (maybe_exclude_function(func))
            continue;

        const char* name = (*walk)->name;
        const char* found_name = get_function_name(func);
        assert(found_name);
        if (found_name == nullptr)
            continue;

        // Only add the first name for each function, and the first function for
        // each name.
        int32 seen = 0;
        seen += seen_func.find(func) != seen_func.end();
        seen += seen_name.find(found_name) != seen_name.end();
        seen += seen_name.find(name) != seen_name.end();
        if (seen < 3)
        {
            seen_func.emplace(func);
            seen_name.emplace(found_name);
            seen_name.emplace(name);
        }
        if (seen > 0)
            continue;

#if defined (VI_MODE)
        // Only list vi commands in vi mode, since the commands are not designed
        // to be generally usable outside of vi mode or when bound to arbitrary
        // custom keys.
        if (!is_vi_mode &&
            (found_name[0] == 'v' && found_name[1] == 'i' && found_name[2] == '-') &&
            strcmp(found_name, "vi-editing-mode") != 0 &&
            strcmp(found_name, "vi-movement-mode") != 0)
        {
            continue;
        }
#endif /* VI_MODE */

        const char* desc;
        int32 cat = keycat_misc;
        get_function_info(func, &desc, &cat);

        if (*offset >= *max)
        {
            *max *= 2;
            collector = (Keyentry *)realloc(collector, sizeof(collector[0]) * *max);
        }

        Keyentry& out = collector[*offset];
        memset(&out, 0, sizeof(out));
        out.sort = MAKELONG(999, 999);
        out.key_name = (char*)calloc(1, 1);
        out.func_name = found_name;
        out.func_desc = desc;
        out.cat = categories ? cat : 0;

        ++(*offset);
    }

    for (auto& macro_desc : s_macro_descriptions)
    {
        if (*offset >= *max)
        {
            *max *= 2;
            collector = (Keyentry *)realloc(collector, sizeof(collector[0]) * *max);
        }

        Keyentry& out = collector[*offset];
        memset(&out, 0, sizeof(out));
        out.sort = MAKELONG(999, 999);
        out.key_name = (char*)calloc(1, 1);
        out.func_name = macro_desc.first;
        out.func_desc = macro_desc.second.c_str();
        out.cat = categories ? keycat_macros : 0;

        ++(*offset);
    }

    return collector;
}

//------------------------------------------------------------------------------
static int32 __cdecl cmp_sort_collector(const void* pv1, const void* pv2)
{
    const Keyentry* p1 = (const Keyentry*)pv1;
    const Keyentry* p2 = (const Keyentry*)pv2;
    int32 cmp;

    // Sort first by modifier keys.
    cmp = (p1->sort >> 16) - (p2->sort >> 16);
    if (cmp)
        return cmp;

    // Next by named key order.
    cmp = (int16)p1->sort - (int16)p2->sort;
    if (cmp)
        return cmp;

    // Next sort by key name (folding case).
    cmp = strcmpi(p1->key_name, p2->key_name);
    if (cmp)
        return cmp;
    cmp = strcmp(p1->key_name, p2->key_name);
    if (cmp)
        return cmp;

    // Finally sort by function name (folding case).
    cmp = strcmpi(p1->func_name, p2->func_name);
    if (cmp)
        return cmp;
    return strcmp(p1->func_name, p2->func_name);
}

//------------------------------------------------------------------------------
static int32 __cdecl cmp_sort_collector_cat(const void* pv1, const void* pv2)
{
    const Keyentry* p1 = (const Keyentry*)pv1;
    const Keyentry* p2 = (const Keyentry*)pv2;
    int32 cmp;

    // Sort first by category.
    cmp = (p1->cat) - (p2->cat);
    if (cmp)
        return cmp;

    return cmp_sort_collector(pv1, pv2);
}

//------------------------------------------------------------------------------
static void pad_with_spaces(str_base& str, uint32 pad_to)
{
    const uint32 len = cell_count(str.c_str());
    if (len < pad_to)
        concat_spaces(str, pad_to - len);
}

//------------------------------------------------------------------------------
static void append_key_macro(str_base& s, const char* macro, const int32 limit)
{
    const int32 limit_ellipsis = limit - ellipsis_cells;
    int32 truncate_len = 0;
    uint32 count = 0;

    wcwidth_iter iter(macro);
    while (int32 c = iter.next())
    {
        const int32 w = iter.character_wcwidth_onectrl();
        if (count <= limit_ellipsis)
            truncate_len = s.length();
        if (count > limit)
            break;
        s.concat(iter.character_pointer(), iter.character_length());
        count += w;
    }

    if (count > limit)
    {
        s.truncate(truncate_len);
        s << ellipsis;
    }
}

//------------------------------------------------------------------------------
static bool is_keyentry_equivalent(const Keyentry* map, int32 a, int32 b)
{
    const Keyentry& ka = map[a];
    const Keyentry& kb = map[b];
    // Empty names are not equivalent (necessary to report unbound commands).
    if ((!ka.key_name || !*ka.key_name) || (!kb.key_name || !*kb.key_name))
        return false;
    // Different names are not equivalent.
    if (!ka.key_name != !kb.key_name || strcmp(ka.key_name, kb.key_name) != 0)
        return false;
    assert(ka.sort == kb.sort);
    assert(!ka.macro_text == !kb.macro_text && (!ka.macro_text || strcmp(ka.macro_text, kb.macro_text) == 0));
    assert(!ka.func_name == !kb.func_name && (!ka.func_name || strcmp(ka.func_name, kb.func_name) == 0));
    assert(!ka.func_desc == !kb.func_desc && (!ka.func_desc || strcmp(ka.func_desc, kb.func_desc) == 0));
    assert(ka.prefix == kb.prefix);
    return true;
}

//------------------------------------------------------------------------------
struct key_binding_info { str_moveable name; str_moveable binding; const char* desc; const char* cat; };
void show_key_bindings(bool friendly, int32 mode, std::vector<key_binding_info>* out=nullptr)
{
    bool show_categories = out || !!(mode & 1);
    bool show_descriptions = out || !!(mode & 2);

    struct show_line
    {
        show_line(const char* heading, const Keyentry* entries, int32 count, int32 step)
            : m_heading(heading), m_entries(entries), m_count(count), m_step(step) {}

        const char* const m_heading;
        const Keyentry* const m_entries;
        const int32 m_count;
        const int32 m_step;
    };

    Keymap map = rl_get_keymap();
    int32 offset = 1;
    int32 max_collect = 64;
    Keyentry* collector = (Keyentry*)malloc(sizeof(Keyentry) * max_collect);
    memset(&collector[0], 0, sizeof(collector[0]));

    // Collect the functions in the active keymap.
    str<32> keyseq;
    std::vector<str_moveable> warnings;
    collector = collect_keymap(map, collector, &offset, &max_collect, keyseq, friendly, show_categories, (map == emacs_standard_keymap) ? &warnings : nullptr);

    // Maybe include unbound commands.
    if (mode & 4)
        collector = collect_functions(collector, &offset, &max_collect, show_categories);

    if (!collector)
        return;

    // Sort the collected keymap.
    qsort(collector + 1, offset - 1, sizeof(*collector), out ? cmp_sort_collector : cmp_sort_collector_cat);

    // Remove duplicates; these can happen due to ANYOTHERKEY.
// TODO: But don't remove unbound entries!
    {
        Keyentry* tortoise = collector + 1;
        Keyentry* hare = collector + 1;
        int32 num = 1;
        for (int32 i = 1; i < offset; ++i)
        {
            if (is_keyentry_equivalent(collector, i, i - 1))
            {
                free(tortoise->key_name);
                free(tortoise->macro_text);
            }
            else
            {
                tortoise++;
                num++;
            }
            hare++;
            if (tortoise != hare)
                memcpy(tortoise, hare, sizeof(*hare));
        }
        offset = num;
    }

    // Find the longest key name and function name.
    uint32 longest_key[keycat_MAX] = {};
    uint32 longest_func[keycat_MAX] = {};
    uint32 desc_pad = show_descriptions ? 1 : 0;
    const int32 macro_limit = _rl_screenwidth * 4 / 10;
    for (int32 i = 1; i < offset; ++i)
    {
        const Keyentry& entry = collector[i];
        int32 cat = show_categories ? entry.cat : 0;
        uint32 k = (uint32)strlen(entry.key_name);
        uint32 f = 0;
        if (entry.func_name)
            f = (uint32)strlen(entry.func_name);
        else if (entry.macro_text)
            f = 2 + min<int32>(strlen(entry.macro_text), macro_limit);
        f += desc_pad;
        if (cat)
        {
            if (longest_key[cat] < k)
                longest_key[cat] = k;
            if (longest_func[cat] < f)
                longest_func[cat] = f;
        }
        if (longest_key[0] < k)
            longest_key[0] = k;
        if (longest_func[0] < f)
            longest_func[0] = f;
    }

    // Calculate columns.
    auto longest = [&longest_key, &longest_func, desc_pad](int32 cat) { return longest_key[cat] + 3 + longest_func[cat] + 2 + desc_pad; };
    const int32 max_width = out ? 0 : __complete_get_screenwidth();
    const int32 columns_that_fit = show_descriptions ? 0 : max_width / longest(0);
    const int32 columns = max(1, columns_that_fit);

    // Calculate rows.
    std::vector<show_line> lines;
    {
        const bool vertical = out ? true : !_rl_print_completions_horizontally;

        int32 cat = -1;
        int32 sub_begin = 1;
        for (int32 k = 1; true; ++k)
        {
            const bool last = (k >= offset);
            const int32 this_cat = (last ? -2 :
                                  !show_categories ? -1 :
                                  collector[k].cat);

            if (this_cat != cat)
            {
                int32 sub_count = k - sub_begin;
                if (sub_count)
                {
                    const int32 rows = (sub_count + (columns - 1)) / columns;
                    const int32 index_step = vertical ? rows : 1;

                    if (show_categories)
                    {
                        const char* const heading = c_headings[cat];
                        if (lines.size())
                            lines.emplace_back(nullptr, nullptr, 0, 0); // Blank line.
                        lines.emplace_back(heading, nullptr, 0, 0);
                    }

                    for (int32 i = 0; i < rows; ++i)
                    {
                        int32 index = (vertical ? i : (i * columns));
                        const Keyentry* entries = collector + index + sub_begin;
                        const int32 count = min<int32>(sub_count, columns);
                        lines.emplace_back(nullptr, entries, count, index_step);
                        sub_count -= count;
                    }
                }

                assert(sub_count == 0);

                sub_begin = k;
                cat = this_cat;
            }

            if (last)
                break;
        }
    }

    // Move cursor past the input line.
    if (!out)
        end_prompt(true/*crlf*/);

    // Display any warnings.
    if (!out)
    {
        g_pager->start_pager(*g_printer);
        if (warnings.size() > 0)
        {
            bool stop = false;

            if (!g_pager->on_print_lines(*g_printer, 1))
                stop = true;
            else
                g_printer->print("\n");

            int32 num_warnings = stop ? 0 : int32(warnings.size());
            for (int32 i = 0; i < num_warnings; ++i)
            {
                str_moveable& s = warnings[i];

                // Ask the pager what to do.
                int32 lines = ((s.length() - 14 + max_width - 1) / max_width); // -14 for escape codes.
                if (!g_pager->on_print_lines(*g_printer, lines))
                {
                    stop = true;
                    break;
                }

                // Print the warning.
                g_printer->print(s.c_str(), s.length());
                g_printer->print("\n");
            }

            if (stop || !g_pager->on_print_lines(*g_printer, 1))
                lines.clear();
            else
                g_printer->print("\n");
        }
    }

    // Display the matches.
    str<> tmp;
    str<> str;
    key_binding_info info;
    int32 cat = - 1;
    for (auto const& line : lines)
    {
        const int32 cat = !out && show_categories && show_descriptions && line.m_entries ? line.m_entries->cat : 0;

        // Ask the pager what to do.
        if (!out)
        {
            int32 lines = 1;
            if (!columns_that_fit && line.m_entries)
            {
                const Keyentry& entry = *line.m_entries;
                int32 len = longest(cat);
                if (show_descriptions && len + 1 >= max_width)
                {
                    len = longest_key[cat] + 3;
                    if (entry.func_name)
                        len += int32(strlen(entry.func_name));
                    else
                        // TODO: strlen() isn't right; it's UTF8!
                        len += min(2 + int32(strlen(entry.macro_text)), 32);
                }
                lines += len / g_printer->get_columns();
            }
            if (!g_pager->on_print_lines(*g_printer, lines))
                break;
        }

        // Print the row.
        if (line.m_entries)
        {
            int32 index = 0;
            for (int32 j = line.m_count; j-- > 0;)
            {
                // Key name.
                const Keyentry& entry = line.m_entries[index];
                str.clear();
                if (!out && entry.warning)
                    str << "\x1b[7m";
                str << entry.key_name;
                if (!out && entry.warning)
                    str << "\x1b[m";
                pad_with_spaces(str, longest_key[cat]);
                if (out)
                {
                    info.name = str.c_str();
                    str.clear();
                }

                // Separator.
                if (!out)
                    str << " : ";

                // Key binding.
                if (entry.func_name)
                    str << entry.func_name;
                if (entry.macro_text)
                {
                    str << "\"";
                    append_key_macro(str, entry.macro_text, macro_limit);
                    str << "\"";
                }
                const int32 len_name_binding = longest(cat);
                bool show_desc = (show_descriptions && entry.func_desc && len_name_binding + 1 < max_width);
                if (j || show_desc)
                    pad_with_spaces(str, len_name_binding);
                if (out)
                {
                    info.binding = str.c_str();
                    str.clear();
                }

                // Command description.
                if (!out)
                {
                    if (show_desc)
                    {
                        ellipsify(entry.func_desc, max_width - 1 - len_name_binding, tmp, false/*expand_ctrl*/);
                        str << tmp.c_str();
                    }
                }
                else
                {
                    info.desc = entry.func_desc;
                    info.cat = c_headings[entry.cat];
                }

                // Print the key binding.
                if (!out)
                    g_printer->print(str.c_str(), str.length());
                else
                    out->emplace_back(std::move(info));

                index += line.m_step;
            }
        }
        else if (line.m_heading)
        {
            if (!out)
            {
                str.clear();
                str << "\x1b[7m" << line.m_heading << "\x1b[m";
                g_printer->print(str.c_str(), str.length());
            }
        }

        if (!out)
            g_printer->print("\n");
    }

    if (!out)
    {
        // Reset (not redraw!) so that transient prompt draws properly.
        rl_reset_line_state();
    }

    // Tidy up (N.B. the first match is a placeholder and shouldn't be freed).
    while (--offset)
    {
        free(collector[offset].key_name);
        free(collector[offset].macro_text);
    }
    free(collector);
}

//------------------------------------------------------------------------------
static bool cmp_sort_command_bindings(const command_binding& a, const command_binding& b)
{
    int32 cmp;

    // Sort first by modifier keys.
    cmp = (a.sort >> 16) - (b.sort >> 16);
    if (cmp)
        return cmp < 0;

    // Next by named key order.
    cmp = (int16)a.sort - (int16)b.sort;
    if (cmp)
        return cmp < 0;

    // Finally sort by key name (folding case).
    cmp = strcmpi(a.key.c_str(), b.key.c_str());
    if (cmp)
        return cmp < 0;
    return strcmp(a.key.c_str(), b.key.c_str());
}

//------------------------------------------------------------------------------
bool get_command_bindings(const char* command, bool friendly, str_base& _desc, str_base& _category, std::vector<str_moveable>& keys)
{
    if (!ensure_keydesc_map())
        return false;

    const char* desc = nullptr;
    int32 cat = keycat_none;
    rl_command_func_t* func = rl_named_function(command);
    bool lookup_macro = false;
    str<> macro;

    if (func)
    {
        get_function_info(func, &desc, &cat);
    }
    else if (command[0] == '"' && command[1])
    {
        const size_t len = strlen(command);
        if (command[len - 1] == '"')
        {
            lookup_macro = true;
            macro.concat(command + 1, len - 2);
            desc = lookup_macro_description(macro.c_str());
            cat = keycat_macros;
        }
    }
    else
        return false;

    _desc = desc;
    _category = c_headings[cat];

    filter_keymap filter;
    filter.command = func;
    filter.macro = lookup_macro ? macro.c_str() : nullptr;

    str<32> keyseq;
    Keymap map = rl_get_keymap();
    std::vector<command_binding> bindings;
    collect_command_bindings(map, filter, friendly, keyseq, bindings);

    std::sort(bindings.begin(), bindings.end(), cmp_sort_command_bindings);

    for (const auto& binding : bindings)
        keys.emplace_back(binding.key.c_str());

    return true;
}

//------------------------------------------------------------------------------
int32 show_rl_help(int32, int32)
{
    int32 mode = rl_explicit_arg ? rl_numeric_arg : 3;
    show_key_bindings(true/*friendly*/, mode);
    return 0;
}

//------------------------------------------------------------------------------
int32 show_rl_help_raw(int32, int32)
{
    int32 mode = rl_explicit_arg ? rl_numeric_arg : 3;
    show_key_bindings(false/*friendly*/, mode);
    return 0;
}

//------------------------------------------------------------------------------
int32 clink_what_is(int32, int32)
{
    if (!ensure_keydesc_map())
        return 0;

    // Move cursor past the input line.
    end_prompt(true/*crlf*/);

    int32 type;
    rl_command_func_t* func = nullptr;
    str<32> keyseq;
    bool not_bound = false;

    const bool friendly = !rl_explicit_arg || !rl_numeric_arg;

    str<> s;
    while (true)
    {
        int32 sort = 0;
        char* key_name = nullptr;
        int32 key;

        key = read_key_direct(false/*wait*/);
        if (key < 0)
        {
            if (not_bound)
                break;

            g_printer->print("\r\x1b[Kwhat-is: ");
            if (keyseq.length())
                translate_keyseq(keyseq.c_str(), keyseq.length(), &key_name, friendly, sort);
            if (key_name)
            {
                s.clear();
                s << "\x1b[0;1m" << key_name << "\x1b[m,";
                g_printer->print(s.c_str(), s.length());
                free(key_name);
                key_name = nullptr;
            }

            key = read_key_direct(true/*wait*/);
        }

        if (key < 0)
        {
            func = nullptr;
            break;
        }

        concat_key_string(key, keyseq);

        func = rl_function_of_keyseq_len(keyseq.c_str(), keyseq.length(), nullptr, &type);
        if (type != ISKMAP)
        {
            if (func)
                break;
            // Read until no more input to capture the full typed key sequence.
            not_bound = true;
        }
    }

    if (clink_is_signaled())
        return 0;

    g_printer->print("\r\x1b[J");

    if (keyseq.length())
    {
        int32 sort = 0;
        char* key_name = nullptr;
        if (keyseq.length())
            translate_keyseq(keyseq.c_str(), keyseq.length(), &key_name, friendly, sort);
        if (key_name)
        {
            s.clear();
            s << "\x1b[0;1m" << key_name << "\x1b[m" << " : ";
            free(key_name);

            if (!func)
            {
                s << "key is not bound";
            }
            else if (type == ISFUNC)
            {
                const char* desc = nullptr;
                const char* name = get_function_name(func);
                if (!name && func == rl_insert)
                {
                    name = "key inserts itself";
                }
                else
                {
                    if (!get_function_info(func, &desc, nullptr) || !desc || !*desc)
                        desc = nullptr;
                }
                if (name)
                    s << "\x1b[0;1m" << name << "\x1b[m";
                else
                    s << "unknown command";
                if (desc)
                    s << " -- " << desc;
            }
            else
            {
                char* macro = _rl_untranslate_macro_value((char*)func, 0);
                const char* desc = lookup_macro_description(macro);
                if (macro)
                    s << "\"" << macro << "\"";
                else
                    s << "unknown macro";
                if (desc)
                    s << " -- " << desc;
                free(macro);
            }

            str<> tmp;
            ecma48_wrapper wrapper(s.c_str(), _rl_screenwidth - 2);
            while (wrapper.next(tmp))
                g_printer->print(tmp.c_str(), tmp.length());
        }
    }

    // Reset (not redraw!) so that transient prompt draws properly.
    rl_reset_line_state();

    return 0;
}
