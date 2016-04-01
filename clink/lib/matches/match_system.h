// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class match_generator;
class matches;
class line_state;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual bool generate(const line_state& line, matches& out) = 0;

private:
};



//------------------------------------------------------------------------------
class match_system
{
public:
                            match_system();
                            ~match_system();
    bool                    add_generator(int priority, match_generator& generator);

    /* MODE4 */ void        generate_matches(const char* line, int cursor, class matches& result) const;

private:
    friend class            match_pipeline;
    unsigned int            get_generator_count() const;
    match_generator*        get_generator(unsigned int index) const;

private:
    struct item
    {
        void*               ptr;
        unsigned int        key;
    };

    template <int SIZE>
    using items = fixed_array<item, SIZE>;

    items<8>                m_generators;

    friend class match_pipeline;
};
