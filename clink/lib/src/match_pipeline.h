// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class line_states;
class match_generator;
class matches_impl;

//------------------------------------------------------------------------------
class match_pipeline
{
public:
                        match_pipeline(matches_impl& matches);
    void                reset() const;
    void                set_no_sort();
    void                generate(const line_states& states, match_generator* generator, bool old_filtering=false) const;
    void                restrict(str_base& needle) const;
    void                restrict(char** keep_matches) const;
    void                select(const char* needle) const;
    void                sort() const;

private:
    matches_impl&       m_matches;
};

//------------------------------------------------------------------------------
int32 get_log_generators();
