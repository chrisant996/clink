// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_generator.h"
#include "line_state.h"
#include "matches.h"

#include <core/globber.h>
#include <core/path.h>
#include <core/settings.h>

static setting_bool g_hidden(
    "files.hidden",
    "Include hidden files",
    "", // MODE4
    true);

static setting_bool g_system(
    "files.system",
    "Include system files",
    "", // MODE4
    false);


//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    static class : public match_generator
    {
        virtual bool generate(const line_state& line, match_builder& builder) override
        {
            str<288> buffer;
            line.get_end_word(buffer);
            buffer << "*";

            globber globber(buffer.c_str());
            globber.hidden(g_hidden.get());
            globber.system(g_system.get());
            while (globber.next(buffer, false))
                builder.add_match(buffer.c_str());

            return true;
        }
    } instance;

    return instance;
}
