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
};
void    clink_add_funmap_entry(const char *name, rl_command_func_t *function, int cat, const char* desc);

//------------------------------------------------------------------------------
int     show_rl_help(int, int);
int     show_rl_help_raw(int, int);

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
int     clink_up_directory(int count, int invoking_key);
int     clink_insert_dot_dot(int count, int invoking_key);

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
void    cua_clear_selection();
bool    cua_point_in_selection(int in);
int     cua_selection_event_hook(int event);
void    cua_after_command(bool force_clear=false);
int     cua_backward_char(int count, int invoking_key);
int     cua_forward_char(int count, int invoking_key);
int     cua_backward_word(int count, int invoking_key);
int     cua_forward_word(int count, int invoking_key);
int     cua_beg_of_line(int count, int invoking_key);
int     cua_end_of_line(int count, int invoking_key);
int     cua_copy(int count, int invoking_key);
int     cua_cut(int count, int invoking_key);
