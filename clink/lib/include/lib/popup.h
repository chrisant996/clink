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
                    popup_results(popup_result result=popup_result::cancel, int32 index=-1, const char* text=nullptr);
    void            clear();

    popup_result    m_result;
    int32           m_index;
    str_moveable    m_text;
};

//------------------------------------------------------------------------------
typedef bool (*del_callback_t)(int32 index);

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
    uint32          height = 0;
    uint32          width = 0;
    bool            reverse = false;
    int32           search_mode = -1; // Values correspond to the "clink.popup_search_mode" enum setting.
    popup_colors    colors;
};

//------------------------------------------------------------------------------
struct entry_info
{
    int32           index;
    bool            marked;
};

//------------------------------------------------------------------------------
extern popup_results activate_directories_text_list(const char** dirs, int32 count);
extern popup_results activate_history_text_list(const char** history, int32 count, int32 index, entry_info* infos, bool win_history);
extern popup_results activate_text_list(const char* title, const char** entries, int32 count, int32 current, bool has_columns, const popup_config* config=nullptr);
