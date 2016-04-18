// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class match_generator;
class matches;
template <typename T> class array;

//------------------------------------------------------------------------------
class match_pipeline
{
public:
                        match_pipeline(matches& matches);
    void                reset();
    void                generate(const line_state& state, const array<match_generator*>& generators);
    void                select(const char* needle);
    void                sort();

private:
    matches&            m_matches;
};
