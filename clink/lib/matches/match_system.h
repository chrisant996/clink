// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class match_generator;
class matches;

//------------------------------------------------------------------------------
class match_system
{
public:
                            match_system();
                            ~match_system();
    bool                    add_generator(match_generator* generator, int priority);
    /* MODE4 */ void        generate_matches(const char* line, int cursor, matches& result) const;

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
