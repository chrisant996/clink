// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <terminal/printer.h>
#include <terminal/terminal.h>
#include <terminal/ecma48_iter.h>
#include "rl_commands.h"
#include "editor_module.h"
#include "pager.h"

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
#include <readline/rlprivate.h>
extern int complete_get_screenwidth(void);
}

#include <vector>
#include <assert.h>

//------------------------------------------------------------------------------
extern printer* g_printer;
extern pager* g_pager;
extern editor_module::result* g_result;

//------------------------------------------------------------------------------
struct Keyentry
{
    int sort;
    char* key_name;
    char* macro_text;
    const char* func_name;
    bool warning;
};

//------------------------------------------------------------------------------
struct Keydesc
{
    Keydesc(const char* name, int cat, const char* desc) : name(name), desc(desc), cat(cat) {}
    const char* name;   // command name
    const char* desc;   // command description
    int cat;            // command category
};

//------------------------------------------------------------------------------
typedef std::map<rl_command_func_t*, struct Keydesc> keydesc_map;
static keydesc_map* s_pmap_keydesc = nullptr;

//------------------------------------------------------------------------------
static const struct {
    const char* name;
    rl_command_func_t* func;
    int cat;
    const char* desc;
} c_func_descriptions[] = {
  { "abort", rl_abort, keycat_basic, "" },
  { "accept-line", rl_newline, keycat_basic, "" },
/* begin_clink_change */
  { "add-history", rl_add_history, keycat_history, "" },
/* end_clink_change */
  { "arrow-key-prefix", rl_arrow_keys, keycat_cursor, "" },
  { "backward-byte", rl_backward_byte, keycat_cursor, "" },
  { "backward-char", rl_backward_char, keycat_cursor, "" },
  { "backward-delete-char", rl_rubout, keycat_basic, "" },
  { "backward-kill-line", rl_backward_kill_line, keycat_basic, "" },
  { "backward-kill-word", rl_backward_kill_word, keycat_basic, "" },
  { "backward-word", rl_backward_word, keycat_cursor, "" },
  { "beginning-of-history", rl_beginning_of_history, keycat_history, "" },
  { "beginning-of-line", rl_beg_of_line, keycat_basic, "" },
  { "bracketed-paste-begin", rl_bracketed_paste_begin, keycat_misc, "" },
  { "call-last-kbd-macro", rl_call_last_kbd_macro, keycat_misc, "" },
  { "capitalize-word", rl_capitalize_word, keycat_misc, "" },
  { "character-search", rl_char_search, keycat_basic, "" },
  { "character-search-backward", rl_backward_char_search, keycat_basic, "" },
  { "clear-display", rl_clear_display, keycat_misc, "" },
  { "clear-screen", rl_clear_screen, keycat_misc, "" },
  { "complete", rl_complete, keycat_completion, "" },
  { "copy-backward-word", rl_copy_backward_word, keycat_misc, "" },
  { "copy-forward-word", rl_copy_forward_word, keycat_misc, "" },
  { "copy-region-as-kill", rl_copy_region_to_kill, keycat_misc, "" },
  { "delete-char", rl_delete, keycat_basic, "" },
  { "delete-char-or-list", rl_delete_or_show_completions, keycat_basic, "" },
  { "delete-horizontal-space", rl_delete_horizontal_space, keycat_basic, "" },
  { "digit-argument", rl_digit_argument, keycat_misc, "" },
  { "do-lowercase-version", rl_do_lowercase_version, keycat_misc, "" },
  { "downcase-word", rl_downcase_word, keycat_misc, "" },
  { "dump-functions", rl_dump_functions, keycat_misc, "" },
  { "dump-macros", rl_dump_macros, keycat_misc, "" },
  { "dump-variables", rl_dump_variables, keycat_misc, "" },
  { "emacs-editing-mode", rl_emacs_editing_mode, keycat_misc, "" },
  { "end-kbd-macro", rl_end_kbd_macro, keycat_misc, "" },
  { "end-of-history", rl_end_of_history, keycat_history, "" },
  { "end-of-line", rl_end_of_line, keycat_basic, "" },
  { "exchange-point-and-mark", rl_exchange_point_and_mark, keycat_misc, "" },
  { "forward-backward-delete-char", rl_rubout_or_delete, keycat_basic, "" },
  { "forward-byte", rl_forward_byte, keycat_basic, "" },
  { "forward-char", rl_forward_char, keycat_basic, "" },
  { "forward-search-history", rl_forward_search_history, keycat_history, "" },
  { "forward-word", rl_forward_word, keycat_basic, "" },
  { "history-search-backward", rl_history_search_backward, keycat_history, "" },
  { "history-search-forward", rl_history_search_forward, keycat_history, "" },
  { "history-substring-search-backward", rl_history_substr_search_backward, keycat_history, "" },
  { "history-substring-search-forward", rl_history_substr_search_forward, keycat_history, "" },
  { "insert-comment", rl_insert_comment, keycat_misc, "" },
  { "insert-completions", rl_insert_completions, keycat_misc, "" },
  { "kill-whole-line", rl_kill_full_line, keycat_basic, "" },
  { "kill-line", rl_kill_line, keycat_basic, "" },
  { "kill-region", rl_kill_region, keycat_basic, "" },
  { "kill-word", rl_kill_word, keycat_basic, "" },
  { "menu-complete", rl_menu_complete, keycat_completion, "" },
  { "menu-complete-backward", rl_backward_menu_complete, keycat_completion, "" },
  { "next-history", rl_get_next_history, keycat_history, "" },
  { "next-screen-line", rl_next_screen_line, keycat_cursor, "" },
  { "non-incremental-forward-search-history", rl_noninc_forward_search, keycat_history, "" },
  { "non-incremental-reverse-search-history", rl_noninc_reverse_search, keycat_history, "" },
  { "non-incremental-forward-search-history-again", rl_noninc_forward_search_again, keycat_history, "" },
  { "non-incremental-reverse-search-history-again", rl_noninc_reverse_search_again, keycat_history, "" },
  { "old-menu-complete", rl_old_menu_complete, keycat_completion, "" },
/* begin_clink_change */
  { "old-menu-complete-backward", rl_backward_old_menu_complete, keycat_completion, "" },
/* end_clink_change */
  { "operate-and-get-next", rl_operate_and_get_next, keycat_history, "" },
  { "overwrite-mode", rl_overwrite_mode, keycat_basic, "" },
#if defined (_WIN32)
  { "paste-from-clipboard", rl_paste_from_clipboard, keycat_basic, "" },
#endif
  { "possible-completions", rl_possible_completions, keycat_completion, "" },
  { "previous-history", rl_get_previous_history, keycat_history, "" },
  { "previous-screen-line", rl_previous_screen_line, keycat_basic, "" },
  { "print-last-kbd-macro", rl_print_last_kbd_macro, keycat_misc, "" },
  { "quoted-insert", rl_quoted_insert, keycat_basic, "" },
  { "re-read-init-file", rl_re_read_init_file, keycat_misc, "" },
  { "redraw-current-line", rl_refresh_line, keycat_misc, "" },
/* begin_clink_change */
  { "remove-history", rl_remove_history, keycat_history, "" },
/* end_clink_change */
  { "reverse-search-history", rl_reverse_search_history, keycat_history, "" },
  { "revert-line", rl_revert_line, keycat_basic, "" },
  //{ "self-insert", rl_insert },
  { "set-mark", rl_set_mark, keycat_misc, "" },
  { "skip-csi-sequence", rl_skip_csi_sequence, keycat_misc, "" },
  { "start-kbd-macro", rl_start_kbd_macro, keycat_misc, "" },
  { "tab-insert", rl_tab_insert, keycat_basic, "" },
  { "tilde-expand", rl_tilde_expand, keycat_completion, "" },
  { "transpose-chars", rl_transpose_chars, keycat_basic, "" },
  { "transpose-words", rl_transpose_words, keycat_basic, "" },
  { "tty-status", rl_tty_status, keycat_misc, "" },
  { "undo", rl_undo_command, keycat_basic, "" },
  { "universal-argument", rl_universal_argument, keycat_misc, "" },
  { "unix-filename-rubout", rl_unix_filename_rubout, keycat_basic, "" },
  { "unix-line-discard", rl_unix_line_discard, keycat_basic, "" },
  { "unix-word-rubout", rl_unix_word_rubout, keycat_basic, "" },
  { "upcase-word", rl_upcase_word, keycat_misc, "" },
  { "yank", rl_yank, keycat_misc, "" },
  { "yank-last-arg", rl_yank_last_arg, keycat_basic, "" },
  { "yank-nth-arg", rl_yank_nth_arg, keycat_misc, "" },
  { "yank-pop", rl_yank_pop, keycat_misc, "" },

#if defined (VI_MODE)
  { "vi-append-eol", rl_vi_append_eol, keycat_none, "" },
  { "vi-append-mode", rl_vi_append_mode, keycat_none, "" },
  { "vi-arg-digit", rl_vi_arg_digit, keycat_none, "" },
  { "vi-back-to-indent", rl_vi_back_to_indent, keycat_none, "" },
  { "vi-backward-bigword", rl_vi_bWord, keycat_none, "" },
  { "vi-backward-word", rl_vi_bword, keycat_none, "" },
  //{ "vi-bWord", rl_vi_bWord },	/* BEWARE: name matching is case insensitive */
  //{ "vi-bword", rl_vi_bword },	/* BEWARE: name matching is case insensitive */
  { "vi-change-case", rl_vi_change_case, keycat_none, "" },
  { "vi-change-char", rl_vi_change_char, keycat_none, "" },
  { "vi-change-to", rl_vi_change_to, keycat_none, "" },
  { "vi-char-search", rl_vi_char_search, keycat_none, "" },
  { "vi-column", rl_vi_column, keycat_none, "" },
  { "vi-complete", rl_vi_complete, keycat_none, "" },
  { "vi-delete", rl_vi_delete, keycat_none, "" },
  { "vi-delete-to", rl_vi_delete_to, keycat_none, "" },
  //{ "vi-eWord", rl_vi_eWord },
  { "vi-editing-mode", rl_vi_editing_mode, keycat_none, "" },
  { "vi-end-bigword", rl_vi_eWord, keycat_none, "" },
  { "vi-end-word", rl_vi_end_word, keycat_none, "" },
  { "vi-eof-maybe", rl_vi_eof_maybe, keycat_none, "" },
  //{ "vi-eword", rl_vi_eword },
  //{ "vi-fWord", rl_vi_fWord },	/* BEWARE: name matching is case insensitive */
  { "vi-fetch-history", rl_vi_fetch_history, keycat_none, "" },
  { "vi-first-print", rl_vi_first_print, keycat_none, "" },
  { "vi-forward-bigword", rl_vi_fWord, keycat_none, "" },
  { "vi-forward-word", rl_vi_fword, keycat_none, "" },
  //{ "vi-fWord", rl_vi_fWord },	/* BEWARE: name matching is case insensitive */
  { "vi-goto-mark", rl_vi_goto_mark, keycat_none, "" },
  { "vi-insert-beg", rl_vi_insert_beg, keycat_none, "" },
  { "vi-insertion-mode", rl_vi_insert_mode, keycat_none, "" },
  { "vi-match", rl_vi_match, keycat_none, "" },
  { "vi-movement-mode", rl_vi_movement_mode, keycat_none, "" },
  { "vi-next-word", rl_vi_next_word, keycat_none, "" },
  { "vi-overstrike", rl_vi_overstrike, keycat_none, "" },
  { "vi-overstrike-delete", rl_vi_overstrike_delete, keycat_none, "" },
  { "vi-prev-word", rl_vi_prev_word, keycat_none, "" },
  { "vi-put", rl_vi_put, keycat_none, "" },
  { "vi-redo", rl_vi_redo, keycat_none, "" },
  { "vi-replace", rl_vi_replace, keycat_none, "" },
  { "vi-rubout", rl_vi_rubout, keycat_none, "" },
  { "vi-search", rl_vi_search, keycat_none, "" },
  { "vi-search-again", rl_vi_search_again, keycat_none, "" },
  { "vi-set-mark", rl_vi_set_mark, keycat_none, "" },
  { "vi-subst", rl_vi_subst, keycat_none, "" },
  { "vi-tilde-expand", rl_vi_tilde_expand, keycat_none, "" },
  { "vi-unix-word-rubout", rl_vi_unix_word_rubout, keycat_none, "" },
  { "vi-yank-arg", rl_vi_yank_arg, keycat_none, "" },
  { "vi-yank-pop", rl_vi_yank_pop, keycat_none, "" },
  { "vi-yank-to", rl_vi_yank_to, keycat_none, "" },
#endif /* VI_MODE */
};

//------------------------------------------------------------------------------
static void ensure_keydesc_map()
{
    static bool s_inited = false;
    if (!s_inited)
    {
        s_inited = true;

        if (!s_pmap_keydesc)
            s_pmap_keydesc = new keydesc_map;

        FUNMAP** funcs = funmap;
        while (*funcs != nullptr)
        {
            FUNMAP* func = *funcs;

            auto& iter = s_pmap_keydesc->find(func->function);
            if (iter == s_pmap_keydesc->end())
                s_pmap_keydesc->emplace(func->function, std::move(Keydesc(func->name, 0, nullptr)));
            else if (!iter->second.name) // Don't overwrite existing name; works around case sensitivity bug with some VI mode commands.
                iter->second.name = func->name;

            ++funcs;
        }

        for (auto const& f : c_func_descriptions)
        {
            auto& iter = s_pmap_keydesc->find(f.func);
            assert(iter != s_pmap_keydesc->end()); // Command no longer exists?
            if (iter != s_pmap_keydesc->end())
            {
                // Command should either not have a name yet, or the name must match.
                assert(!iter->second.name || !strcmp(iter->second.name, f.name));
                iter->second.name = f.name;
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
}

//------------------------------------------------------------------------------
void clink_add_funmap_entry(const char *name, rl_command_func_t *func, int cat, const char* desc)
{
    assert(name);
    assert(func);
    assert(desc);

    rl_add_funmap_entry(name, func);

    if (!s_pmap_keydesc)
        s_pmap_keydesc = new keydesc_map;

    auto& iter = s_pmap_keydesc->find(func);
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
        iter->second.name = desc;
    }
}

//------------------------------------------------------------------------------
static const char* get_function_name(int (*func_addr)(int, int))
{
    auto& iter = s_pmap_keydesc->find(func_addr);
    if (iter != s_pmap_keydesc->end())
        return iter->second.name;

    return nullptr;
}

//------------------------------------------------------------------------------
static const char* get_function_desc(int (*func_addr)(int, int))
{
    auto& iter = s_pmap_keydesc->find(func_addr);
    if (iter != s_pmap_keydesc->end())
        return iter->second.desc;

    return nullptr;
}

//------------------------------------------------------------------------------
static void concat_key_string(int i, str<32>& keyseq)
{
    assert(i >= 0);
    assert(i < 256);

    char c = (unsigned char)i;
    keyseq.concat(&c, 1);
}

//------------------------------------------------------------------------------
static bool translate_keyseq(const char* keyseq, unsigned int len, char** key_name, bool friendly, int& sort)
{
    static const char ctrl_map[] = "@abcdefghijklmnopqrstuvwxyz[\\]^_";

    str<> tmp;
    int order = 0;
    sort = 0;

    // TODO: Produce identical sort order for both friend names and raw names?

    bool first_key = true;
    if (!friendly)
    {
        tmp << "\"";

        unsigned int comma_threshold = 0;
        for (unsigned int i = 0; i < len; i++)
        {
            if (!i && len == 2 && keyseq[0] == 0x1b)
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
        int need_comma = 0;
        const char* keyseq_end = keyseq + len;
        while (keyseq < keyseq_end)
        {
            int keyseq_len;
            int eqclass = 0;
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
                if (*keyseq == '\x1b' && len >= 2)
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;
                    tmp.concat("A-");
                    eqclass |= 4;
                    keyseq++;
                }
                if (*keyseq >= 0 && *keyseq < ' ')
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    tmp.concat("C-", 2);
                    tmp.concat(&ctrl_map[(unsigned char)*keyseq], 1);
                    eqclass |= 2;
                    need_comma = 1;
                    keyseq++;
                }
                else
                {
                    if (need_comma > 0)
                        tmp.concat(",", 1);
                    need_comma = 0;

                    if ((unsigned char)*keyseq == 0x7f)
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
static Keyentry* collect_keymap(
    Keymap map,
    Keyentry* collector,
    int* offset,
    int* max,
    str<32>& keyseq,
    bool friendly,
    std::vector<str_moveable>* warnings)
{
    int i;

    ensure_keydesc_map();

    for (i = 0; i < 256; ++i)
    {
        KEYMAP_ENTRY entry = map[i];
        if (entry.function == nullptr)
            continue;

        // Recursively chain to another keymap.
        if (entry.type == ISKMAP)
        {
            unsigned int old_len = keyseq.length();
            concat_key_string(i, keyseq);
            collector = collect_keymap((Keymap)entry.function, collector, offset, max, keyseq, friendly, warnings);
            keyseq.truncate(old_len);
            continue;
        }

        // Add entry for a function or macro.
        if (entry.type == ISFUNC)
        {
            int blacklisted;
            int j;

            // Blacklist some functions
            int (*blacklist[])(int, int) = {
                rl_insert,
                rl_do_lowercase_version,
                rl_bracketed_paste_begin,
            };

            blacklisted = 0;
            for (j = 0; j < sizeof_array(blacklist); ++j)
            {
                if (blacklist[j] == entry.function)
                {
                    blacklisted = 1;
                    break;
                }
            }

            if (blacklisted)
                continue;
        }

        const char *name = nullptr;
        char *macro = nullptr;
        if (entry.type == ISFUNC)
        {
            name = get_function_name(entry.function);
            if (name == nullptr)
                continue;
        }

        unsigned int old_len = keyseq.length();
        concat_key_string(i, keyseq);

        if (*offset >= *max)
        {
            *max *= 2;
            collector = (Keyentry *)realloc(collector, sizeof(collector[0]) * *max);
        }

        int sort;
        if (translate_keyseq(keyseq.c_str(), keyseq.length(), &collector[*offset].key_name, friendly, sort))
        {
            collector[*offset].sort = sort;
            if (entry.type == ISMACR)
                collector[*offset].macro_text = _rl_untranslate_macro_value((char *)entry.function, 0);
            else
                collector[*offset].macro_text = nullptr;
            collector[*offset].warning = false;

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
                    s << "\x1b[1mwarning:\x1b[m key \x1b[7m" << collector[*offset].key_name << "\x1b[m looks like a typo; did you mean \"" << intent1;
                    if (second)
                        s << intent2;
                    s << "\" instead of \"" << actual1;
                    if (second)
                        s << actual2;
                    s << "\"?";
                    warnings->push_back(std::move(s));
                    collector[*offset].warning = true;
                }
            }

            collector[*offset].func_name = name;
            ++(*offset);
        }

        keyseq.truncate(old_len);
    }

    return collector;
}

//------------------------------------------------------------------------------
static int _cdecl cmp_sort_collector(const void* pv1, const void* pv2)
{
    const Keyentry* p1 = (const Keyentry*)pv1;
    const Keyentry* p2 = (const Keyentry*)pv2;

    // Sort first by modifier keys.
    int cmp = (p1->sort >> 16) - (p2->sort >> 16);
    if (cmp)
        return cmp;

    // Next by named key order.
    cmp = (short int)p1->sort - (short int)p2->sort;
    if (cmp)
        return cmp;

    // Finally sort by key name (folding case).
    cmp = strcmpi(p1->key_name, p2->key_name);
    if (cmp)
        return cmp;
    return strcmp(p1->key_name, p2->key_name);
}

//------------------------------------------------------------------------------
static void pad_with_spaces(str_base& str, unsigned int pad_to)
{
    unsigned int len = cell_count(str.c_str());
    while (len < pad_to)
    {
        const char spaces[] = "                                ";
        const unsigned int available_spaces = sizeof_array(spaces) - 1;
        int space_count = min(pad_to - len, available_spaces);
        str.concat(spaces, space_count);
        len += space_count;
    }
}

//------------------------------------------------------------------------------
static void append_key_macro(str_base& s, const char* macro)
{
    const int limit = 30;
    const int limit_ellipsis = limit - 3;
    int ellipsis = 0;
    unsigned int count = 0;

    str_iter iter(macro);
    const char* p = iter.get_pointer();
    while (int c = iter.next())
    {
        const char* n = iter.get_pointer();
        int w = clink_wcwidth(c);
        if (count <= limit_ellipsis)
            ellipsis = s.length();
        if (count > limit)
            break;
        s.concat(p, int (n - p));
        count += w;
        p = n;
    }

    if (count > limit)
    {
        s.truncate(ellipsis);
        s << "...";
    }
}

//------------------------------------------------------------------------------
void show_key_bindings(bool friendly, std::vector<std::pair<str_moveable, str_moveable>>* out=nullptr)
{
    Keymap map = rl_get_keymap();
    int offset = 1;
    int max_collect = 64;
    Keyentry* collector = (Keyentry*)malloc(sizeof(Keyentry) * max_collect);
    collector[0].key_name = nullptr;
    collector[0].macro_text = nullptr;
    collector[0].func_name = nullptr;

    // Build string up the functions in the active keymap.
    str<32> keyseq;
    std::vector<str_moveable> warnings;
    collector = collect_keymap(map, collector, &offset, &max_collect, keyseq, friendly, (map == emacs_standard_keymap) ? &warnings : nullptr);

    // Sort the collected keymap.
    qsort(collector + 1, offset - 1, sizeof(*collector), cmp_sort_collector);

    // Find the longest key name and function name.
    unsigned int longest_key = 0;
    unsigned int longest_func = 0;
    for (int i = 1; i < offset; ++i)
    {
        unsigned int k = (unsigned int)strlen(collector[i].key_name);
        unsigned int f = 0;
        if (collector[i].func_name)
            f = (unsigned int)strlen(collector[i].func_name);
        else if (collector[i].macro_text)
            f = min(2 + (int)strlen(collector[i].macro_text), 32);
        if (longest_key < k)
            longest_key = k;
        if (longest_func < f)
            longest_func = f;
    }

    // Calculate columns.
    unsigned int longest = longest_key + 3 + longest_func + 2;
    int max_width = out ? 0 : complete_get_screenwidth();
    int columns_that_fit = max_width / longest;
    int columns = max(1, columns_that_fit);
    int total_rows = ((offset - 1) + (columns - 1)) / columns;

    bool vertical = out ? true : !_rl_print_completions_horizontally;
    int index_step = vertical ? total_rows : 1;

    // Move cursor past the input line.
    if (!out)
    {
        _rl_move_vert(_rl_vis_botlin);
        g_printer->print("\n");
    }

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

            int num_warnings = stop ? 0 : int(warnings.size());
            for (int i = 0; i < num_warnings; ++i)
            {
                str_moveable& s = warnings[i];

                // Ask the pager what to do.
                int lines = ((s.length() - 14 + max_width - 1) / max_width); // -14 for escape codes.
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
                total_rows = 0;
            else
                g_printer->print("\n");
        }
    }

    // Display the matches.
    str<> str;
    std::pair<str_moveable, str_moveable> pair;
    for (int i = 0; i < total_rows; ++i)
    {
        int index = vertical ? i : (i * columns);
        index++;

        // Ask the pager what to do.
        if (!out)
        {
            int lines = 1;
            if (!columns_that_fit)
            {
                int len = 3; // " : "
                len += int(strlen(collector[index].key_name));
                if (collector[index].func_name)
                    len += int(strlen(collector[index].func_name));
                else
                    len += min(2 + int(strlen(collector[index].macro_text)), 32);
                lines += len / g_printer->get_columns();
            }
            if (!g_pager->on_print_lines(*g_printer, lines))
                break;
        }

        // Print the row.
        for (int j = columns - 1; j >= 0; --j)
        {
            if (index >= offset)
                continue;

            // Key name.
            const Keyentry& entry = collector[index];
            str.clear();
            if (!out && entry.warning)
                str << "\x1b[7m";
            str << entry.key_name;
            if (!out && entry.warning)
                str << "\x1b[m";
            pad_with_spaces(str, longest_key);
            if (out)
                pair.first = str.c_str();

            // Separator.
            if (!out)
                str << " : ";
            else
                str.clear();

            // Key binding.
            if (entry.func_name)
                str << entry.func_name;
            if (entry.macro_text)
            {
                str << "\"";
                append_key_macro(str, entry.macro_text);
                str << "\"";
            }
            if (out)
                pair.second = str.c_str();

            // Pad column with spaces.
            if (j)
                pad_with_spaces(str, longest);

            // Print the key binding.
            if (!out)
                g_printer->print(str.c_str(), str.length());
            else
                out->emplace_back(std::move(pair));

            index += index_step;
        }

        if (!out)
            g_printer->print("\n");
    }

    if (!out)
    {
        g_printer->print("\n");
        g_result->redraw();
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
int show_rl_help(int, int)
{
    show_key_bindings(true/*friendly*/);
    return 0;
}

//------------------------------------------------------------------------------
int show_rl_help_raw(int, int)
{
    show_key_bindings(false/*friendly*/);
    return 0;
}
