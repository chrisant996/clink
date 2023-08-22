// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/os.h> // Prevent S_IFLNK macro redefinition.

extern "C" {
#include <compat/config.h>
#include <readline/readline.h> // For rl_command_func_t.
}

//------------------------------------------------------------------------------
enum
{
    keycat_none,
    keycat_basic,
    keycat_cursor,
    keycat_completion,
    keycat_history,
    keycat_killyank,
    keycat_select,
    keycat_scroll,
    keycat_misc,
    keycat_macros,
    keycat_MAX
};
void    clink_add_funmap_entry(const char *name, rl_command_func_t *function, int32 cat, const char* desc);

//------------------------------------------------------------------------------
int32   macro_hook_func(const char* macro);
void    reset_command_states();
bool    is_force_reload_scripts();
void    clear_force_reload_scripts();
int32   force_reload_scripts();

//------------------------------------------------------------------------------
int32   host_add_history(int32, const char* line);
int32   host_remove_history(int32 rl_history_index, const char* line);

//------------------------------------------------------------------------------
int32   show_rl_help(int32, int32);
int32   show_rl_help_raw(int32, int32);
int32   clink_what_is(int32, int32);

//------------------------------------------------------------------------------
int32   clink_reload(int32, int32);
int32   clink_reset_line(int32, int32);
int32   clink_exit(int32 count, int32 invoking_key);
int32   clink_ctrl_c(int32 count, int32 invoking_key);
int32   clink_paste(int32 count, int32 invoking_key);
int32   clink_copy_line(int32 count, int32 invoking_key);
int32   clink_copy_word(int32 count, int32 invoking_key);
int32   clink_copy_cwd(int32 count, int32 invoking_key);
int32   clink_expand_env_var(int32 count, int32 invoking_key);
int32   clink_expand_doskey_alias(int32 count, int32 invoking_key);
int32   clink_expand_history(int32 count, int32 invoking_key);
int32   clink_expand_history_and_alias(int32 count, int32 invoking_key);
int32   clink_expand_line(int32 count, int32 invoking_key);
int32   clink_up_directory(int32 count, int32 invoking_key);
int32   clink_insert_dot_dot(int32 count, int32 invoking_key);
int32   clink_shift_space(int32 count, int32 invoking_key);
int32   clink_magic_suggest_space(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   clink_scroll_line_up(int32 count, int32 invoking_key);
int32   clink_scroll_line_down(int32 count, int32 invoking_key);
int32   clink_scroll_page_up(int32 count, int32 invoking_key);
int32   clink_scroll_page_down(int32 count, int32 invoking_key);
int32   clink_scroll_top(int32 count, int32 invoking_key);
int32   clink_scroll_bottom(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   clink_find_conhost(int32 count, int32 invoking_key);
int32   clink_mark_conhost(int32 count, int32 invoking_key);
int32   clink_selectall_conhost(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   clink_popup_directories(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   clink_complete_numbers(int32 count, int32 invoking_key);
int32   clink_menu_complete_numbers(int32 count, int32 invoking_key);
int32   clink_menu_complete_numbers_backward(int32 count, int32 invoking_key);
int32   clink_old_menu_complete_numbers(int32 count, int32 invoking_key);
int32   clink_old_menu_complete_numbers_backward(int32 count, int32 invoking_key);
int32   clink_popup_complete_numbers(int32 count, int32 invoking_key);
int32   clink_popup_show_help(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
bool    point_in_select_complete(int32 in);
int32   clink_select_complete(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
bool    cua_clear_selection();
bool    cua_set_selection(int32 anchor, int32 point);
int32   cua_get_anchor();
bool    cua_point_in_selection(int32 in);
int32   cua_selection_event_hook(int32 event);
void    cua_after_command(bool force_clear=false);
int32   cua_previous_screen_line(int32 count, int32 invoking_key);
int32   cua_next_screen_line(int32 count, int32 invoking_key);
int32   cua_backward_char(int32 count, int32 invoking_key);
int32   cua_forward_char(int32 count, int32 invoking_key);
int32   cua_backward_word(int32 count, int32 invoking_key);
int32   cua_forward_word(int32 count, int32 invoking_key);
int32   cua_select_word(int32 count, int32 invoking_key);
int32   cua_beg_of_line(int32 count, int32 invoking_key);
int32   cua_end_of_line(int32 count, int32 invoking_key);
int32   cua_select_all(int32 count, int32 invoking_key);
int32   cua_copy(int32 count, int32 invoking_key);
int32   cua_cut(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   clink_forward_word(int32 count, int32 invoking_key);
int32   clink_forward_char(int32 count, int32 invoking_key);
int32   clink_forward_byte(int32 count, int32 invoking_key);
int32   clink_end_of_line(int32 count, int32 invoking_key);
int32   clink_insert_suggested_full_word(int32 count, int32 invoking_key);
int32   clink_insert_suggested_line(int32 count, int32 invoking_key);
int32   clink_insert_suggested_word(int32 count, int32 invoking_key);
int32   clink_accept_suggested_line(int32 count, int32 invoking_key);
int32   clink_popup_history(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   win_f1(int32 count, int32 invoking_key);
int32   win_f2(int32 count, int32 invoking_key);
int32   win_f3(int32 count, int32 invoking_key);
int32   win_f4(int32 count, int32 invoking_key);
int32   win_f6(int32 count, int32 invoking_key);
int32   win_f7(int32 count, int32 invoking_key);
int32   win_f9(int32 count, int32 invoking_key);
bool    win_fn_callback_pending();

//------------------------------------------------------------------------------
bool    is_globbing_wild();     // Expand wildcards in alternative_matches()?
bool    is_literal_wild();      // Avoid appending star in alternative_matches()?
int32   glob_complete_word(int32 count, int32 invoking_key);
int32   glob_expand_word(int32 count, int32 invoking_key);
int32   glob_list_expansions(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   edit_and_execute_command(int32 count, int32 invoking_key);
int32   magic_space(int32 count, int32 invoking_key);

//------------------------------------------------------------------------------
int32   clink_diagnostics(int32 count, int32 invoking_key);
