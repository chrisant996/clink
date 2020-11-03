// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

class line_state;
class match_generator;
class matches_impl;
template <typename T> class array;

//------------------------------------------------------------------------------
class match_pipeline
{
public:
                        match_pipeline(matches_impl& matches);
    void                reset() const;
    void                generate(const line_state& state, const array<match_generator*>& generators) const;
    void                select(const char* needle) const;
    void                sort() const;

private:
    matches_impl&       m_matches;
};
