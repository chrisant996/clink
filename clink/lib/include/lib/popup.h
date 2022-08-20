// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

//------------------------------------------------------------------------------
enum class popup_result
{
    error = -1,
    cancel,
    select,
    use,
};

//------------------------------------------------------------------------------
enum class popup_items_mode
{
    simple,
    descriptions,
    display_filter,
};

//------------------------------------------------------------------------------
struct popup_results
{
                    popup_results(popup_result result=popup_result::cancel, int index=-1, const char* text=nullptr);
    void            clear();

    popup_result    m_result;
    int             m_index;
    str_moveable    m_text;
};

//------------------------------------------------------------------------------
popup_result do_popup_list(
    const char* title,
    const char** items,
    int num_items,
    int len_prefix,
    bool completing,
    bool auto_complete,
    bool reverse_find,
    int& current,
    const char*& out,
    popup_items_mode mode=popup_items_mode::simple);
