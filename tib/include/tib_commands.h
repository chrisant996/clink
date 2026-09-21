// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

#include "tib_base.h"
#include "tib_bindings.h"

namespace tib {

int32_t abort(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t accept_line(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t begin_of_line(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t end_of_line(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t backward_char(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t forward_char(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t backward_word(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t forward_word(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t backward_bigword(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t forward_bigword(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t screen_line_down(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t screen_line_up(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t del_char_left(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t del_char_right(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t del_word_left(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t del_word_right(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t del_bigword_left(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t del_bigword_right(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t redo(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t undo(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t undo_all(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t clear_selection(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t select_all(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t select_word(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t select_bigword(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_begin_of_line(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_end_of_line(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_backward_char(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_forward_char(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_backward_word(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_forward_word(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_screen_line_down(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t cua_screen_line_up(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t set_mark(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t exchange_caret_and_mark(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t cut(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t copy(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t paste(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t quoted_insert(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t toggle_overwrite_mode(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t transpose_chars(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t transpose_words(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t transpose_bigwords(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t upper_case(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t lower_case(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t capitalize(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t toggle_case(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t digit_argument(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;
int32_t universal_argument(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t mouse_input(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t redisplay(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

int32_t lorem_ipsum(tib::editor_context& ctx, int32_t key, const char* name, const binding_params* params) noexcept;

std::shared_ptr<tib::key_table_list> make_default_key_table(bool numeric_argument=false);

}
