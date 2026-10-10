// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/os.h> // Prevent S_IFLNK macro redefinition.

#include <tib.h>

//------------------------------------------------------------------------------
void    init_editor_commands();

//------------------------------------------------------------------------------
void    reset_command_states();
int32   read_key_direct(bool wait);

//------------------------------------------------------------------------------
int32   host_add_history(int32, const char* line, const char** out_timestamp=nullptr);
int32   host_remove_history(int32 rl_history_index, const char* line);

//------------------------------------------------------------------------------
int32_t show_rl_help(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t show_rl_help_raw(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_dump_functions(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_dump_macros(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_what_is(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32   clink_newline(int32 count, int32 invoking_key); // TODO-TIB: temporary placeholder.
int32_t clink_accept_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_reload(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_reset_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_exit(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_ctrl_c(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_paste(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_copy_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_copy_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_copy_cwd(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_expand_env_var(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_expand_doskey_alias(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_expand_history(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_expand_history_and_alias(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_expand_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_up_directory(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_insert_dot_dot(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_shift_space(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_magic_suggest_space(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_toggle_slashes(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_scroll_line_up(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_scroll_line_down(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_scroll_page_up(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_scroll_page_down(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_scroll_top(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_scroll_bottom(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_find_conhost(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_mark_conhost(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_selectall_conhost(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_popup_directories(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_complete_numbers(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_menu_complete_numbers(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_menu_complete_numbers_backward(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_old_menu_complete_numbers(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_old_menu_complete_numbers_backward(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32   clink_popup_complete_numbers(int32 count, int32 invoking_key);
int32_t clink_popup_show_help(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
bool    point_in_select_complete(int32 in);
int32_t clink_select_complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_toggle_suggestion_list(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_show_suggestion_list(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_cancel_suggestion_list(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t cua_forward_char(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_forward_bigword(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_forward_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_forward_char(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
#if 0
int32   clink_forward_byte(int32 count, int32 invoking_key);
#endif
int32_t clink_end_of_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_insert_suggested_full_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_insert_suggested_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_insert_suggested_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_accept_suggested_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
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
// Readline compatibility.
void rl_sync_with_clink();
int32_t backward_kill_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t forward_kill_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t backward_kill_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t forward_kill_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t kill_full_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t kill_region(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t copy_backward_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t copy_forward_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t copy_region_to_kill(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t rubout_or_delete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t insert_close(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t insert_comment(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t unix_filename_rubout(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t unix_line_discard(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t unix_word_rubout(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t yank(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
// int32_t yank_last_arg(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
// int32_t yank_nth_arg(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t yank_pop(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t re_read_init_file(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t refresh_line(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_tilde_expand(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_tilde_expand(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clear_display(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clear_screen(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t possible_completions(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t insert_completions(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t menu_complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t backward_menu_complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t old_menu_complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t backward_old_menu_complete(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t dump_functions(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t dump_macros(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t dump_variables(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
bool    is_globbing_wild();     // Expand wildcards in alternative_matches()?
bool    is_literal_wild();      // Avoid appending star in alternative_matches()?
int32_t glob_complete_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t glob_expand_word(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t glob_list_expansions(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t edit_and_execute_command(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t magic_space(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

//------------------------------------------------------------------------------
int32_t clink_diagnostics(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_diagnostics_output(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
