// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

// vim: set et ts=4 sw=4 cino={0s:

#pragma once

// Instead of directly using termcap, we use helper functions that encapsulate
// the contractual expectations.

namespace tib {

extern const char c_hide_cursor[];
extern const char c_show_cursor[];

bool ensure_term_caps();
void uninit_term_caps();

const char* term_row_col(int32_t row, int32_t col);
const char* term_col(int32_t col);
const char* term_erase_to_eol();
const char* term_move_up(int32_t num_rows);
const char* term_move_down(int32_t num_rows);

coord get_terminal_size();

}
