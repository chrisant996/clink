// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
int     show_rl_help(int, int);

//------------------------------------------------------------------------------
int     clink_reset_line(int, int);
int     clink_exit(int count, int invoking_key);
int     clink_ctrl_c(int count, int invoking_key);
int     clink_paste(int count, int invoking_key);
int     clink_copy_line(int count, int invoking_key);
int     clink_copy_cwd(int count, int invoking_key);
int     clink_up_directory(int count, int invoking_key);
int     clink_insert_dot_dot(int count, int invoking_key);

//------------------------------------------------------------------------------
int     clink_scroll_line_up(int count, int invoking_key);
int     clink_scroll_line_down(int count, int invoking_key);
int     clink_scroll_page_up(int count, int invoking_key);
int     clink_scroll_page_down(int count, int invoking_key);
int     clink_scroll_top(int count, int invoking_key);
int     clink_scroll_bottom(int count, int invoking_key);
