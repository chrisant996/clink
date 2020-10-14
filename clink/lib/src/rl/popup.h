// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum class popup_list_result
{
    error = -1,
    cancel,
    select,
    use,
};

//------------------------------------------------------------------------------
popup_list_result do_popup_list(
    const char* title,
    const char** items,
    int num_items,
    int len_prefix,
    int past_flag,
    bool completing,
    bool auto_complete,
    int& current,
    str_base& out);

