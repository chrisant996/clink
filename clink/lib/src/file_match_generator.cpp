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



//------------------------------------------------------------------------------
static class : public match_generator
{
    virtual bool generate(const line_state& line, match_builder& builder) override
    {
        str<288> root;
        line.get_end_word(root);

        bool expanded_tilde;
        {
            bool just_tilde = (strcmp(root.c_str(), "~") == 0);
            char* expanded_root = tilde_expand(root.c_str());
            expanded_tilde = (expanded_root && strcmp(expanded_root, root.c_str()) != 0);
            if (expanded_tilde)
            {
                root = expanded_root;
                if (just_tilde)
                    path::append(root, "");
            }
            free(expanded_root);

            if (just_tilde && rl_completion_type == '?')
                return true;
        }

        path::normalise_separators(root);

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

        return true;
    }

    virtual void get_word_break_info(const line_state& line, word_break_info& info) const override
    {
        str_iter end_word = line.get_end_word();
        const char* start = end_word.get_pointer();
        const char* c = start + end_word.length();

        if (is_tilde(start, end_word.length()))
        {
            // Tilde by itself should be expanded, so keep the whole word.
        }
        else if (is_dots(start, end_word.length()))
        {
            // `.` or `..` should be kept so that matches can include `.` or
            // `..` directories.  Bash includes `.` and `..` but only when those
            // match typed text (i.e. when there's no input text, they are not
            // considered possible matches).
        }
        else
        {
            for (; c > start; --c)
                if (path::is_separator(c[-1]))
                    break;

            if (start[0] && start[1] == ':')
                c = max(start + 2, c);
        }

        info.truncate = 0;
        info.keep = int(c - start);
    }

private:
    static bool is_tilde(const char* word, unsigned int len)
    {
        return (len == 1 && word[0] == '~');
    }

    static bool is_dots(const char* word, unsigned int len)
    {
        if (!advance_ignore_quotes(word, len))
            return false;               // Too short.
        if (word[len] != '.')
            return false;               // No dot at end.

        if (!advance_ignore_quotes(word, len))
            return true;                // Exactly ".".
        if (word[len] == '.' && !advance_ignore_quotes(word, len))
            return true;                // Exactly "..".

        if (path::is_separator(word[len]))
            return true;                // Ends with "\." or "\..".

        return false;                   // Else nope.
    }

    static bool advance_ignore_quotes(const char* word, unsigned int& len)
    {
        if (len)
        {
            unsigned int seek = len - 1;
            while (true)
            {
                // Finding a non-quote is success.
                if (word[seek] != '"')
                {
                    len = seek;
                    return true;
                }
                // Reaching the beginning is failure.
                if (!seek)
                    break;
                seek--;
                // Finding a `\"` digraph is failure.
                if (word[seek] == '\\')
                    break;
            }
        }
        return false;
    }
} g_file_generator;


//------------------------------------------------------------------------------
match_generator& file_match_generator()
{
    return g_file_generator;
}
