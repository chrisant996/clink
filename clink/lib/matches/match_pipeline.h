// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class match_system;
class matches;

//------------------------------------------------------------------------------
class match_pipeline
{
public:
                        match_pipeline(const match_system& system, matches& result);
    void                reset();
    void                generate(const line_state& state);
    void                select(const char* selector_name, const char* needle);
    void                sort(const char* sort_name);

private:
    const match_system& m_system;
    matches&            m_matches;
};
