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
void    clink_add_funmap_entry(const char *name, rl_command_func_t *function, int cat, const char* desc);

//------------------------------------------------------------------------------
int     macro_hook_func(const char* macro);
void    reset_command_states();
bool    is_force_reload_scripts();
void    clear_force_reload_scripts();
int     force_reload_scripts();

//------------------------------------------------------------------------------
int     show_rl_help(int, int);
int     show_rl_help_raw(int, int);
int     clink_what_is(int, int);

//------------------------------------------------------------------------------
int     clink_reload(int, int);
int     clink_reset_line(int, int);
int     clink_exit(int count, int invoking_key);
int     clink_ctrl_c(int count, int invoking_key);
int     clink_paste(int count, int invoking_key);
int     clink_copy_line(int count, int invoking_key);
int     clink_copy_word(int count, int invoking_key);
int     clink_copy_cwd(int count, int invoking_key);
int     clink_expand_env_var(int count, int invoking_key);
int     clink_expand_doskey_alias(int count, int invoking_key);
int     clink_expand_history(int count, int invoking_key);
int     clink_expand_history_and_alias(int count, int invoking_key);
int     clink_expand_line(int count, int invoking_key);
int     clink_up_directory(int count, int invoking_key);
int     clink_insert_dot_dot(int count, int invoking_key);
int     clink_shift_space(int count, int invoking_key);
int     clink_magic_suggest_space(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_scroll_line_up(int count, int invoking_key);
int     clink_scroll_line_down(int count, int invoking_key);
int     clink_scroll_page_up(int count, int invoking_key);
int     clink_scroll_page_down(int count, int invoking_key);
int     clink_scroll_top(int count, int invoking_key);
int     clink_scroll_bottom(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_find_conhost(int count, int invoking_key);
int     clink_mark_conhost(int count, int invoking_key);
int     clink_selectall_conhost(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_popup_directories(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_complete_numbers(int count, int invoking_key);
int     clink_menu_complete_numbers(int count, int invoking_key);
int     clink_menu_complete_numbers_backward(int count, int invoking_key);
int     clink_old_menu_complete_numbers(int count, int invoking_key);
int     clink_old_menu_complete_numbers_backward(int count, int invoking_key);
int     clink_popup_complete_numbers(int count, int invoking_key);
int     clink_popup_show_help(int count, int invoking_key);

//------------------------------------------------------------------------------
bool    point_in_select_complete(int in);
int     clink_select_complete(int count, int invoking_key);

//------------------------------------------------------------------------------
bool    cua_clear_selection();
bool    cua_set_selection(int anchor, int point);
int     cua_get_anchor();
bool    cua_point_in_selection(int in);
int     cua_selection_event_hook(int event);
void    cua_after_command(bool force_clear=false);
int     cua_previous_screen_line(int count, int invoking_key);
int     cua_next_screen_line(int count, int invoking_key);
int     cua_backward_char(int count, int invoking_key);
int     cua_forward_char(int count, int invoking_key);
int     cua_backward_word(int count, int invoking_key);
int     cua_forward_word(int count, int invoking_key);
int     cua_select_word(int count, int invoking_key);
int     cua_beg_of_line(int count, int invoking_key);
int     cua_end_of_line(int count, int invoking_key);
int     cua_select_all(int count, int invoking_key);
int     cua_copy(int count, int invoking_key);
int     cua_cut(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_forward_word(int count, int invoking_key);
int     clink_forward_char(int count, int invoking_key);
int     clink_forward_byte(int count, int invoking_key);
int     clink_end_of_line(int count, int invoking_key);
int     clink_insert_suggestion(int count, int invoking_key);
int     clink_accept_suggestion(int count, int invoking_key);

//------------------------------------------------------------------------------
int     win_f1(int count, int invoking_key);
int     win_f2(int count, int invoking_key);
int     win_f3(int count, int invoking_key);
int     win_f4(int count, int invoking_key);
int     win_f6(int count, int invoking_key);
int     win_f7(int count, int invoking_key);
int     win_f9(int count, int invoking_key);
bool    win_fn_callback_pending();

//------------------------------------------------------------------------------
bool    is_globbing_wild();     // Expand wildcards in alternative_matches()?
bool    is_literal_wild();      // Avoid appending star in alternative_matches()?
int     glob_complete_word(int count, int invoking_key);
int     glob_expand_word(int count, int invoking_key);
int     glob_list_expansions(int count, int invoking_key);

//------------------------------------------------------------------------------
int     edit_and_execute_command(int count, int invoking_key);
int     magic_space(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_diagnostics(int count, int invoking_key);
