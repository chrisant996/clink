// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class line_states;
class match_builder;
struct match_display_filter_entry;
enum class display_filter_flags;

//------------------------------------------------------------------------------
struct word_break_info
{
                    word_break_info() { clear(); }
    void            clear() { truncate = 0; keep = 0; }

    int32           truncate : 16;
    int32           keep : 16;
};

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual bool    generate(const line_states& line, match_builder& builder, bool old_filtering=false) = 0;
    virtual void    get_word_break_info(const line_state& line, word_break_info& info) const = 0;
    virtual bool    match_display_filter(const char* needle, char** matches, match_builder* builder, display_filter_flags flag, bool nosort, bool* old_filtering=nullptr) { return false; }
    virtual bool    filter_matches(char** matches, char completion_type, bool filename_completion_desired) { return false; }

private:
};
