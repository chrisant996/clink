// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <lib/match_generator.h>

class lua_state;

//------------------------------------------------------------------------------
class lua_match_generator
    : public match_generator
{
public:
                    lua_match_generator(lua_state& state);
    virtual         ~lua_match_generator();

    virtual bool    filter_matches(char** matches, char completion_type, bool filename_completion_desired) override;

private:
    virtual bool    generate(const line_states& lines, match_builder& builder, bool old_filtering=false) override;
    virtual void    get_word_break_info(const line_state& line, word_break_info& info) const override;
    virtual bool    match_display_filter(const char* needle, char** matches, match_display_filter_entry*** filtered_matches, display_filter_flags flags, bool nosort, bool* old_filtering=nullptr) override;
    lua_state&      m_state;
};
