// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "matches/match_generator.h"

//------------------------------------------------------------------------------
class file_match_generator
    : public match_generator
{
public:
                    file_match_generator();
    virtual         ~file_match_generator();
    virtual bool    generate(const line_state& line, matches& out) override;

private:
};
