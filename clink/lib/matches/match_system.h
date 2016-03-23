// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <vector>

class match_generator;
class matches;

//------------------------------------------------------------------------------
class match_system
{
public:
                            match_system();
                            ~match_system();
    void                    add_generator(match_generator* generator, int priority);
    void                    generate_matches(const char* line, int cursor, matches& result) const;
    void                    generate_matches(const struct line_state_2& line_state, matches& result) const;

private:
    struct Generator
    {
        match_generator*    generator;
        int                 priority;
    };

    std::vector<Generator>  m_generators;
};
