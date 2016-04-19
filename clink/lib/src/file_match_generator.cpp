// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_generator.h"
#include "line_state.h"
#include "matches.h"

#include <core/globber.h>
#include <core/path.h>

//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    static class : public match_generator
    {
        virtual bool generate(const line_state& line, match_builder& builder) override
        {
            str<MAX_PATH> buffer;
            line.get_end_word(buffer);
            buffer << "*";

            globber globber(buffer.c_str());
            while (globber.next(buffer, false))
                builder.add_match(buffer.c_str());

            return true;
        }
    } instance;

    return instance;
}
