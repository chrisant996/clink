// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class rl_history
{
public:
                    rl_history();
                    ~rl_history();
    void            add(const char* line);

private:
    void            load();
    void            save();
};
