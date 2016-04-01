// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/array.h>

class match_generator;
class match_store;
class matches;
class line_state;
struct match_info;

//------------------------------------------------------------------------------
class match_generator
{
public:
    virtual bool generate(const line_state& line, matches& out) = 0;

private:
};

match_generator& file_match_generator();



//------------------------------------------------------------------------------
class match_selector
{
public:
    virtual unsigned int select(const char* needle, const match_store& store, match_info* infos, int count) = 0;
};

match_selector& normal_match_selector();



//------------------------------------------------------------------------------
class match_system
{
public:
                            match_system();
                            ~match_system();
    bool                    add_generator(int priority, match_generator& generator);
    bool                    add_selector(const char* name, match_selector& selector);
    bool                    add_sorter(const char* name, match_sorter& sorter);

    /* MODE4 */ void        generate_matches(const char* line, int cursor, class matches& result) const;

private:
    friend class            match_pipeline;
    unsigned int            get_generator_count() const;
    match_generator*        get_generator(unsigned int index) const;
    match_selector*         get_selector(const char* name) const;

private:
    struct item
    {
        void*               ptr;
        unsigned int        key;
    };

    template <int SIZE>
    using items = fixed_array<item, SIZE>;

    items<8>                m_generators;
    items<4>                m_selectors;
};
