// Copyright (c) 2022 Christopher Antos
//
// Portions Copyright (C) 1985-2022 Free Software Foundation, Inc.
// https://github.com/coreutils/coreutils/blob/3067a9293af07ba2cf1ababe6b4482196717f806/src/ls.c
// License: https://github.com/coreutils/coreutils/blob/5b9d747261590ffde5f47fcf8cef06ee5bb5df63/COPYING

#include "pch.h"
#include "display_matches.h"
#include "matches_lookaside.h"
#include "match_adapter.h"
#include "column_widths.h"
#include "ellipsify.h"

#include <core/base.h>
#include <core/path.h>
#include <core/debugheap.h>

extern "C" {
#include <readline/readline.h>
#include <readline/rldefs.h>
#include <readline/rlprivate.h>
int32 __complete_get_screenwidth(void);
int32 __fnwidth(const char *string);
char* __printable_part(char* pathname);
int32 __stat_char(const char *filename, char match_type);
}

#include <vector>
#include <assert.h>

#define ELLIPSIS_LEN ellipsis_len

//------------------------------------------------------------------------------
/* Information about filling a column.  */
struct column_info
{
    bool valid_len;
    width_t line_len;
    width_t *col_arr;
};

//------------------------------------------------------------------------------
/* Array with information about column filledness.  */
static struct column_info *s_column_info = nullptr;



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
        _rl_colored_stats ||
#endif
        _rl_complete_mark_directories))
    {
        char *sep = rl_last_path_separator(match);
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

    /* Currently allocated columns in column_info.  */
    static size_t column_info_alloc;

    // Constrain number of matches.
    if (max_matches < 0)
        return false;
    if (max_matches && count > max_matches)
        return false;

    // Constrain memory usage and computation time.
    if (max_cols > 50)
        max_cols = 50;

    if (column_info_alloc < max_cols)
    {
        size_t new_column_info_alloc;
        width_t *p;

        dbg_ignore_scope(snapshot, "calculate_columns");
        s_column_info = (column_info*)realloc(s_column_info, max_cols * sizeof *s_column_info);
        new_column_info_alloc = max_cols;

        /* Allocate the new size_t objects by computing the triangle
           formula n * (n + 1) / 2, except that we don't need to
           allocate the part of the triangle that we've already
           allocated.  Check for address arithmetic overflow.  */
        {
            size_t column_info_growth = new_column_info_alloc - column_info_alloc;
            size_t s = column_info_alloc + 1 + new_column_info_alloc;
            size_t t = s * column_info_growth;
            if (s < new_column_info_alloc || t / column_info_growth != s)
                return false;
            p = (width_t*)malloc(t / 2 * sizeof *p);
            if (!p)
                return false;
        }

        /* Grow the triangle by parceling out the cells just allocated.  */
        for (i = column_info_alloc; i < new_column_info_alloc; i++)
        {
            s_column_info[i].col_arr = p;
            p += i + 1;
        }

        column_info_alloc = new_column_info_alloc;
    }

    for (i = 0; i < max_cols; ++i)
    {
        size_t j;

        s_column_info[i].valid_len = 1;
        s_column_info[i].line_len = (i + 1) * (1 + col_padding);
        for (j = 0; j <= i; ++j)
            s_column_info[i].col_arr[j] = (1 + col_padding);
    }

    return true;
}

//------------------------------------------------------------------------------
// Calculate the number of columns needed to represent the current set of
// matches in the current display width.
column_widths calculate_columns(match_adapter* adapter, int32 max_matches, bool one_column, bool omit_desc, width_t extra, int32 presuf)
{
    column_widths widths;

    const bool has_descriptions = !omit_desc && adapter->has_descriptions();
    const width_t col_padding = has_descriptions ? 4 : 2;
    const width_t desc_padding = has_descriptions ? 2 : 0;

    /* Determine the max possible number of display columns.  */
    const bool vertical = !_rl_print_completions_horizontally;
    const size_t line_length = __complete_get_screenwidth();
    size_t max_idx = line_length;
    max_idx = (max_idx + col_padding) / (1 + col_padding);

    /* Normally the maximum number of columns is determined by the
       screen width.  But if few files are available this might limit it
       as well.  */
    const size_t count = adapter->get_match_count();
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
        adapter->get_lcd(lcd);
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
    if (sind == 0 && _rl_colored_completion_prefix > 0)
    {
        str<32> lcd;
        adapter->get_lcd(lcd);
        if (lcd.length() > 0 && !path::is_separator(lcd.c_str()[lcd.length() - 1]))
        {
            const char* t = __printable_part(const_cast<char*>(lcd.c_str()));
            common_length = __fnwidth(t);
            sind = strlen(t);
        }
    }
#endif

    /* Compute the maximum number of possible columns.  */
    int32 max_match = 0;    // Longest match width in cells.
    int32 max_desc = 0;     // Longest description width in cells.
    int32 max_len = 0;      // Longest combined match and desc width in cells.
    int32 i = 0;
    for (size_t filesno = 0; filesno < count; ++filesno, ++i)
    {
        size_t len = extra;

        int32 cdelta = condense_delta;
        match_type type = adapter->get_match_type(i);
        const char *match = adapter->get_match(i);
        bool append = adapter->is_append_display(i);
        if (adapter->use_display(i, type, append))
        {
            if (append)
            {
                len += printable_len(match, type);
                len += adapter->get_match_visible_display(i);
            }
            else if (presuf)
            {
                len += adapter->get_match_visible_display(i);
            }
            else
            {
                len += adapter->get_match_visible_display(i);
                cdelta = 0;
            }
        }
        else
        {
            len += printable_len(match, type);
        }

        if (cdelta)
        {
            const char *visible = __printable_part(const_cast<char*>(match));
            if (strlen(visible) > sind)
            {
                assert(len >= cdelta);
                len -= cdelta;
            }
        }

        if (max_match < len)
            max_match = len;

        if (has_descriptions)
        {
            const uint32 desc_cells = adapter->get_match_visible_description(i);
            if (desc_cells)
            {
                len += desc_padding + desc_cells;
#ifdef USE_DESC_PARENS
                len += 2; // For parentheses.
#endif
                if (max_desc < desc_cells)
                    max_desc = desc_cells;
            }
        }

        if (max_len < len)
            max_len = len;

        if (fixed_cols)
            continue;

        size_t max_valid = -1;
        for (size_t i = 0; i < max_cols; ++i)
        {
            if (s_column_info[i].valid_len)
            {
                const size_t idx = (vertical
                                    ? filesno / ((count + i) / (i + 1))
                                    : filesno % (i + 1));
                const width_t real_length = len + (idx == i ? 0 : col_padding);

                if (s_column_info[i].col_arr[idx] < real_length)
                {
                    s_column_info[i].line_len += (real_length - s_column_info[i].col_arr[idx]);
                    s_column_info[i].col_arr[idx] = real_length;
                    s_column_info[i].valid_len = (s_column_info[i].line_len < line_length);
                }

                if (s_column_info[i].valid_len)
                    max_valid = i;
            }
        }

        if (max_cols > max_valid + 1)
            max_cols = max_valid + 1;
    }

    widths.m_col_padding = col_padding;
    widths.m_desc_padding = desc_padding;
    widths.m_sind = sind;
    widths.m_max_len = max_len;
    widths.m_max_match = max_match;
    widths.m_max_desc = max_desc;
    widths.m_can_condense = can_condense;

    if (fixed_cols || max_cols <= 0)
    {
        const size_t col_max = max_len + col_padding;
        const size_t limit = one_column ? 1 : max<size_t>((line_length + col_padding - 1) / col_max, 1);
        for (size_t i = 0; i < limit; ++i)
            widths.m_widths.push_back(col_max);
    }
    else
    {
        /* Find maximum allowed columns.  */
        size_t cols;
        for (cols = max_cols; 1 < cols; --cols)
        {
            if (s_column_info[cols - 1].valid_len)
                break;
        }

        size_t remove_padding = cols - 1;
        for (size_t i = 0; i < cols; ++i, --remove_padding)
            widths.m_widths.push_back(s_column_info[cols - 1].col_arr[i] - (remove_padding ? col_padding : 0));
    }

    widths.m_right_justify = widths.num_columns() > 1 || widths.m_max_match > (line_length * 4) / 10;

    return widths;
}
