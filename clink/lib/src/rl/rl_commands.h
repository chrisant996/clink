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
int32   clink_toggle_slashes(int32 count, int32 invoking_key);

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
int32_t clink_selectall_conhost(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

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
int32_t possible_completions(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;

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
int32_t clink_diagnostics(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
int32_t clink_diagnostics_output(tib::editor_context& ctx, int32_t key, const char* name, const tib::binding_params* params) noexcept;
