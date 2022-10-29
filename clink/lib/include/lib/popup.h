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
struct popup_results
{
                    popup_results(popup_result result=popup_result::cancel, int index=-1, const char* text=nullptr);
    void            clear();

    popup_result    m_result;
    int             m_index;
    str_moveable    m_text;
};

//------------------------------------------------------------------------------
typedef bool (*del_callback_t)(int index);

//------------------------------------------------------------------------------
struct popup_colors
{
    str<32>         items;
    str<32>         desc;
    str<32>         border;
    str<32>         header;
    str<32>         footer;
    str<32>         select;
    str<32>         selectdesc;
    str<32>         mark;
    str<32>         selectmark;
};

//------------------------------------------------------------------------------
struct popup_config
{
    del_callback_t  del_callback = nullptr;
    unsigned int    height = 0;
    unsigned int    width = 0;
    bool            reverse = false;
    popup_colors    colors;
};

//------------------------------------------------------------------------------
struct entry_info
{
    int             index;
    bool            marked;
};

//------------------------------------------------------------------------------
extern popup_results activate_directories_text_list(const char** dirs, int count);
extern popup_results activate_history_text_list(const char** history, int count, int index, entry_info* infos, bool win_history);
extern popup_results activate_text_list(const char* title, const char** entries, int count, int current, bool has_columns, const popup_config* config=nullptr);
