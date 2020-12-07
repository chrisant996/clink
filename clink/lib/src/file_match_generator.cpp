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
#include <readline/readline.h>

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
// UI thread to somehow stop a long running operation (maybe with Ctrl+Break).
//
// Or how about just don't run the match generator pipeline until a completion
// command is invoked.
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
        str<288> root;
        line.get_end_word(root);

        bool expanded_tilde;
        {
            char* expanded_root = tilde_expand(root.c_str());
            expanded_tilde = (expanded_root && strcmp(expanded_root, root.c_str()) != 0);
            if (expanded_tilde)
                root = expanded_root;
            free(expanded_root);
        }

        path::normalise(root);

        if (path::is_separator(root[0]) && path::is_separator(root[1]))
            if (!g_glob_unc.get())
                return true;

        root << "*";

        int st_mode = 0;
        int attr = 0;
        globber globber(root.c_str());
        globber.hidden(g_glob_hidden.get());
        globber.system(g_glob_system.get());

        path::get_directory(root);
        unsigned int root_len = root.length();

        if (expanded_tilde)
        {
            extern bool collapse_tilde(const char* in, str_base& out, bool force);
            str<288> collapsed;
            if (collapse_tilde(root.c_str(), collapsed, false))
                root = collapsed.c_str();
        }

        str<288> buffer;
        while (globber.next(buffer, false, &st_mode, &attr))
        {
            root.truncate(root_len);
            path::append(root, buffer.c_str());
            builder.add_match(root.c_str(), to_match_type(st_mode, attr));
        }

        return false;
    }

    virtual void get_word_break_info(const line_state& line, word_break_info& info) const override
    {
        str_iter end_word = line.get_end_word();
        const char* start = end_word.get_pointer();

        const char* c = start + end_word.length();
        for (; c > start; --c)
            if (path::is_separator(c[-1]))
                break;

        if (start[0] && start[1] == ':')
            c = max(start + 2, c);

        info.truncate = 0;
        info.keep = int(c - start);
    }
} g_file_generator;


//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    return g_file_generator;
}
