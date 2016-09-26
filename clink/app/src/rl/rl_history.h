// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/singleton.h>

class str_base;

//------------------------------------------------------------------------------
class rl_history
    : private singleton<const rl_history>
{
public:
                    rl_history();
                    ~rl_history();
    void            add(const char* line);
    int             expand(const char* line, str_base& out);
    void            load(const char* file);
    void            save(const char* file);
    unsigned int    get_count() const;
};
