// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_generator.h"
#include "line_state.h"
#include "matches.h"

#include <core/base.h>
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

// TODO: dream up a way around performance problems that UNC paths pose.
// For example, maybe use a background thread to collect matches, and allow the
// UI thread to somehow a long running operation (maybe with Ctrl+Break).
setting_bool g_glob_unc(
    "files.unc_paths",
    "Enables UNC/network path matches",
    "UNC (network) paths can cause Clink to stutter slightly when it tries to\n"
    "generate matches. Enable this if matching UNC paths is required.",
    false);



//------------------------------------------------------------------------------
static class : public match_generator
{
    virtual bool generate(const line_state& line, match_builder& builder) override
    {
        str<288> buffer;
        line.get_end_word(buffer);

        if (path::is_separator(buffer[0]) && path::is_separator(buffer[1]))
            if (!g_glob_unc.get())
                return true;

        buffer << "*";

        globber globber(buffer.c_str());
        globber.hidden(g_glob_hidden.get());
        globber.system(g_glob_system.get());
        while (globber.next(buffer, false))
            builder.add_match(buffer.c_str(), match_type::none);

        return true;
    }

    virtual int get_prefix_length(const line_state& line) const override
    {
        str_iter end_word = line.get_end_word();
        const char* start = end_word.get_pointer();

        const char* c = start + end_word.length();
        for (; c > start; --c)
            if (path::is_separator(c[-1]))
                break;

        if (start[0] && start[1] == ':')
            c = max(start + 2, c);

        return int(c - start);
    }
} g_file_generator;


//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    return g_file_generator;
}
