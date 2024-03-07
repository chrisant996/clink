// Copyright (c) 2022 Christopher Antos
//
// Portions Copyright (C) 1985-2022 Free Software Foundation, Inc.
// https://github.com/coreutils/coreutils/blob/3067a9293af07ba2cf1ababe6b4482196717f806/src/ls.c
// License: https://github.com/coreutils/coreutils/blob/5b9d747261590ffde5f47fcf8cef06ee5bb5df63/COPYING

#include "pch.h"
#include "display_matches.h"
#include "matches_lookaside.h"
#include "match_adapter.h"
#include "match_colors.h"
#include "column_widths.h"
#include "ellipsify.h"
#include "scroll_car.h"

#include <core/base.h>
#include <core/path.h>
#include <core/os.h>
#include <core/debugheap.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
int __complete_get_screenwidth(void);
char* __printable_part(char* pathname);
int __stat_char(const char* filename, char match_type);
}

#include <vector>
#include <assert.h>

#define ELLIPSIS_LEN ellipsis_len

//------------------------------------------------------------------------------
/* Information about filling a column.  */
struct column_info
{
    width_t a_len;
    width_t b_len;
};
struct columns_info
{
    bool valid_len;
    width_t line_len;
    column_info* col_arr;
};

//------------------------------------------------------------------------------
/* Array with information about column filledness.  */
static struct columns_info* s_columns_info = nullptr;



//------------------------------------------------------------------------------
int32 printable_len(const char* match, match_type type)
{
    const char* temp = __printable_part((char*)match);
    int32 len = __fnwidth(temp);

    // Use the match type to determine whether there will be a visible stat
    // character, and include it in the max length calculation.
    int32 vis_stat = -1;
    if (is_match_type(type, match_type::dir) && (
#if defined (VISIBLE_STATS)
        rl_visible_stats ||
#endif
#if defined (COLOR_SUPPORT)
        using_match_colors() ||
#endif
        _rl_complete_mark_directories))
    {
        char* sep = rl_last_path_separator(match);
        vis_stat = (!sep || sep[1]);
    }
#if defined (VISIBLE_STATS)
    else if (rl_visible_stats && rl_filename_display_desired)
        vis_stat = __stat_char (match, static_cast<match_type_intrinsic>(type));
#endif
    if (vis_stat > 0)
        len++;

    return len;
}

//------------------------------------------------------------------------------
/* Allocate enough column info suitable for the current number of
   files and display columns, and initialize the info to represent the
   narrowest possible columns.  */
static bool init_column_info(int32 max_matches, size_t& max_cols, size_t count, width_t col_padding)
{
    size_t i;

    /* Currently allocated columns in columns_info.  */
    static size_t columns_info_alloc;

    // Constrain number of matches.
    if (max_matches < 0)
        return false;
    if (max_matches && count > max_matches)
        return false;

    // Constrain memory usage and computation time.
    if (max_cols > 50)
        max_cols = 50;

    if (columns_info_alloc < max_cols)
    {
        size_t new_column_info_alloc;
        column_info* p;

        dbg_ignore_scope(snapshot, "calculate_columns");
        s_columns_info = (columns_info*)realloc(s_columns_info, max_cols * sizeof(*s_columns_info));
        new_column_info_alloc = max_cols;

        /* Allocate the new size_t objects by computing the triangle
           formula n * (n + 1) / 2, except that we don't need to
           allocate the part of the triangle that we've already
           allocated.  Check for address arithmetic overflow.  */
        {
            size_t column_info_growth = new_column_info_alloc - columns_info_alloc;
            size_t s = columns_info_alloc + 1 + new_column_info_alloc;
            size_t t = s * column_info_growth;
            if (s < new_column_info_alloc || t / column_info_growth != s)
                return false;
            p = (column_info*)malloc(t / 2 * sizeof(*p));
            if (!p)
                return false;
        }

        /* Grow the triangle by parceling out the cells just allocated.  */
        for (i = columns_info_alloc; i < new_column_info_alloc; i++)
        {
            s_columns_info[i].col_arr = p;
            p += i + 1;
        }

        columns_info_alloc = new_column_info_alloc;
    }

    for (i = 0; i < max_cols; ++i)
    {
        size_t j;

        s_columns_info[i].valid_len = 1;
        s_columns_info[i].line_len = (i + 1) * (1 + col_padding);
        for (j = 0; j <= i; ++j)
        {
            auto& arr = s_columns_info[i].col_arr[j];
            arr.a_len = 1;
            arr.b_len = col_padding;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Calculate the number of columns needed to represent the current set of
// matches in the current display width.
column_widths calculate_columns(const match_adapter& adapter, int32 max_matches, bool one_column, bool omit_desc, width_t extra, int32 presuf)
{
    column_widths widths;

    const bool has_descriptions = !omit_desc && adapter.has_descriptions();
    const width_t col_padding = has_descriptions ? 4 : 2;

    /* Determine the max possible number of display columns.  */
    const bool vertical = !_rl_print_completions_horizontally;
#ifdef SHOW_VERT_SCROLLBARS
    // NOTE:  This reserves space for selectcomplete_impl's vertical scrollbar
    // even in display_matches, so their layouts match.
    const size_t real_screen_width = __complete_get_screenwidth();
    const size_t screen_width = real_screen_width - (use_vert_scrollbars() && !has_descriptions && real_screen_width > 1);
#else
    const size_t screen_width = __complete_get_screenwidth();
#endif
    const size_t line_length = screen_width + (col_padding - 1);
    const size_t max_idx = line_length / (1 + col_padding);

    /* Normally the maximum number of columns is determined by the
       screen width.  But if few files are available this might limit it
       as well.  */
    const size_t count = adapter.get_match_count();
    size_t max_cols = count < max_idx ? count : max_idx;

    const bool fixed_cols = !init_column_info(max_matches, max_cols, count, col_padding) || one_column;

    // Find the length of the prefix common to all items: length as displayed
    // characters (common_length) and as a byte index into the matches (sind).
    int32 sind = 0;
    int32 common_length = 0;
    int32 condense_delta = 0;
    bool can_condense = false;
    if (_rl_completion_prefix_display_length > 0)
    {
        str<32> lcd;
        adapter.get_lcd(lcd);
        if (lcd.length() > 0 && !path::is_separator(lcd.c_str()[lcd.length() - 1]))
        {
            const char* t = __printable_part(const_cast<char*>(lcd.c_str()));
            common_length = __fnwidth(t);
            sind = strlen(t);
        }

        can_condense = (common_length > _rl_completion_prefix_display_length && common_length > ELLIPSIS_LEN);
        if (can_condense)
            condense_delta = common_length - ELLIPSIS_LEN;
        else
            common_length = sind = 0;
    }

#if defined(COLOR_SUPPORT)
    if (sind == 0 && get_completion_prefix_color())
    {
        str<32> lcd;
        adapter.get_lcd(lcd);
        if (lcd.length() > 0 && !path::is_separator(lcd.c_str()[lcd.length() - 1]))
        {
            const char* t = __printable_part(const_cast<char*>(lcd.c_str()));
            common_length = __fnwidth(t);
            sind = strlen(t);
        }
    }
#endif

    /* Compute whether descriptions can be right justified.  */
    bool no_right_justify = !has_descriptions;
    struct no_right_justify_maxes { width_t max_match; width_t max_desc; };
    std::vector<no_right_justify_maxes> no_right_justify_cols;
    if (!no_right_justify)
    {
        for (size_t i = 0; i < count; ++i)
        {
            // Three adjacent spaces in a description could mean there's an
            // attempt to align formatted columns.  For example, like the
            // "clink set" match generator in clink\app\scripts\self.lua in
            // the set_handler() function.
            const char* desc = adapter.get_match_description(i);
            if (desc && strstr(desc, "   "))
            {
                no_right_justify = true;
                break;
            }
        }
    }
    if (no_right_justify)
        no_right_justify_cols.resize(max_cols);
    const width_t desc_padding = !has_descriptions ? 0 : !no_right_justify ? 2 : 4;
#ifdef USE_DESC_PARENS
    const width_t paren_cells = no_right_justify ? 0 : 2; // For parentheses around right-justified descriptions.
#else
    const width_t paren_cells = 0;
#endif

    /* Compute the maximum number of possible columns.  */
    int32 max_match = 0;    // Longest match width in cells.
    int32 max_len = 0;      // Longest combined match and desc width in cells.
    for (size_t filesno = 0; filesno < count; ++filesno)
    {
        width_t match_len = extra;

        int32 cdelta = condense_delta;
        match_type type = adapter.get_match_type(filesno);
        const char* match = adapter.get_match(filesno);
        bool append = adapter.is_append_display(filesno);
        if (adapter.use_display(filesno, type, append))
        {
            if (append)
            {
                match_len += printable_len(match, type);
                match_len += adapter.get_match_visible_display(filesno);
            }
            else if (presuf)
            {
                match_len += adapter.get_match_visible_display(filesno);
            }
            else
            {
                match_len += adapter.get_match_visible_display(filesno);
                cdelta = 0;
            }
        }
        else
        {
            match_len += printable_len(match, type);
        }

        if (cdelta)
        {
            const char* visible = __printable_part(const_cast<char*>(match));
            if (strlen(visible) > sind)
            {
                assert(match_len >= cdelta);
                match_len -= cdelta;
            }
        }

        if (max_match < match_len)
            max_match = match_len;

        width_t desc_len = 0;
        if (has_descriptions)
        {
            desc_len = min<uint32>(1024, adapter.get_match_visible_description(filesno));
            if (desc_len)
                desc_len += desc_padding + paren_cells;
        }

        if (max_len < match_len + desc_len)
            max_len = match_len + desc_len;

        if (fixed_cols)
            continue;

        size_t max_valid = -1;
        for (size_t j = 0; j < max_cols; ++j)
        {
            if (s_columns_info[j].valid_len)
            {
                const size_t idx = (vertical
                                    ? filesno / ((count + j) / (j + 1))
                                    : filesno % (j + 1));
                assert(idx <= j);
                auto& info = s_columns_info[j];
                auto& arr = info.col_arr[idx];

                const width_t a_len = no_right_justify ? max(arr.a_len, match_len) : match_len + desc_len;
                const width_t b_len = no_right_justify ? desc_len + col_padding : col_padding;

                if (arr.a_len < a_len)
                {
                    info.line_len += a_len - arr.a_len;
                    arr.a_len = a_len;
                    info.valid_len = (info.line_len < line_length);
                }
                if (arr.b_len < b_len)
                {
                    info.line_len += b_len - arr.b_len;
                    arr.b_len = b_len;
                    info.valid_len = (info.line_len < line_length);
                }

                if (info.valid_len)
                    max_valid = j;
            }
        }

        if (max_cols > max_valid + 1)
            max_cols = max_valid + 1;
    }

    widths.m_col_padding = col_padding;
    widths.m_desc_padding = desc_padding;
    widths.m_sind = sind;
    widths.m_can_condense = can_condense;

    size_t limit;
    const bool variable_widths = !(fixed_cols || max_cols <= 0);
    if (!variable_widths)
    {
        const size_t col_max = max_len + col_padding;
        limit = one_column ? 1 : max<size_t>(line_length / col_max, 1);
        for (size_t i = 0; i < limit; ++i)
        {
            widths.m_widths.push_back(max_len);
            widths.m_max_match_len_in_column.push_back(max_match);
        }
    }
    else
    {
        /* Find maximum allowed columns.  */
        for (limit = max_cols; 1 < limit; --limit)
        {
            if (s_columns_info[limit - 1].valid_len)
                break;
        }

        const auto& columns_info = s_columns_info[limit - 1];
        for (size_t i = 0; i < limit; ++i)
        {
            const auto& arr = columns_info.col_arr[i];
            widths.m_widths.push_back(arr.a_len + arr.b_len - col_padding);
            widths.m_max_match_len_in_column.push_back(arr.a_len);
        }
    }

    widths.m_right_justify = (!no_right_justify &&
                              (widths.num_columns() > 1 || max_match > (screen_width * 4) / 10) &&
                              (widths.m_widths[0] < screen_width - 2));

    if (!no_right_justify && !widths.m_right_justify && variable_widths)
    {
        // Can't right justify after all; rebuild m_max_match_len_in_column.
        widths.m_max_match_len_in_column.clear();
        for (size_t i = 0; i < limit; ++i)
            widths.m_max_match_len_in_column.push_back(max_match);
    }

    return widths;
}
