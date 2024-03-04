// Copyright (c) 2022 Christopher Antos
//
// Portions Copyright (C) 1985-2022 Free Software Foundation, Inc.
// https://github.com/coreutils/coreutils/blob/3067a9293af07ba2cf1ababe6b4482196717f806/src/ls.c
// License: https://github.com/coreutils/coreutils/blob/5b9d747261590ffde5f47fcf8cef06ee5bb5df63/COPYING

#pragma once

#include <vector>

#define USE_DESC_PARENS

class match_adapter;

//------------------------------------------------------------------------------
typedef unsigned short width_t;

//------------------------------------------------------------------------------
struct column_widths
{
    size_t                  num_columns() const { return m_widths.size(); }
    width_t                 column_width(size_t i) const { return m_widths[i]; }
    width_t                 max_match_len(size_t i) const { return m_max_match_len_in_column[i]; }
    std::vector<width_t>    m_widths;
    std::vector<width_t>    m_max_match_len_in_column;
    width_t                 m_col_padding = 0;  // Padding between columns.
    width_t                 m_desc_padding = 0; // Min padding between match and description.
    width_t                 m_sind = 0;         // LCD length.
    bool                    m_can_condense = false;
    bool                    m_right_justify = false;
};

//------------------------------------------------------------------------------
// Calculates column widths to fit as many columns of matches as possible.
// MAX_MATCHES < 0 makes all columns the same width.
// MAX_MATCHES > 0 disables fitting if the matches exceeds MAX_MATCHES.
// MAX_MATCHES == 0 is unlimited.
column_widths calculate_columns(
    const match_adapter& adapter,
    int32 max_matches=0,
    bool one_column=false,
    bool omit_desc=false,
    width_t extra=0,
    int32 presuf=0);
