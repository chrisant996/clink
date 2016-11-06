// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_generator.h"
#include "line_state.h"
#include "matches.h"

#include <core/globber.h>
#include <core/path.h>
#include <core/settings.h>

setting_bool g_glob_hidden(
    "files.hidden",
    "Include hidden files",
    "Includes or excludes files with the 'hidden' attribute set when generating\n"
    "file lists.",
    true);

setting_bool g_glob_system(
    "files.system",
    "Include system files",
    "Includes or excludes files with the 'system' attribute set when generating\n"
    "file lists.",
    false);


//------------------------------------------------------------------------------
static class : public match_generator
{
    virtual bool generate(const line_state& line, match_builder& builder) override
    {
        str<288> buffer;
        line.get_end_word(buffer);
        buffer << "*";

        globber globber(buffer.c_str());
        globber.hidden(g_glob_hidden.get());
        globber.system(g_glob_system.get());
        while (globber.next(buffer, false))
            builder.add_match(buffer.c_str());

        return true;
    }

    virtual int get_prefix_length(const char* start, int length) const override
    {
        const char* c = start + length;
        for (; c > start; --c)
            if (path::is_separator(c[-1]))
                break;

        return int(c - start);
    }
} g_file_generator;


//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    return g_file_generator;
}
