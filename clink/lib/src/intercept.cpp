// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "intercept.h"

#include <core/path.h>
#include <core/os.h>
#include <core/str.h>

extern "C" {
#include <compat/config.h>
#include <readline/readline.h>
}

//------------------------------------------------------------------------------
static bool parse_line_token(str_base& out, const char* line)
{
    out.clear();

    // Skip leading whitespace.
    while (*line == ' ' || *line == '\t')
        line++;

    // Parse the line text.
    bool expand = false;
    bool first_component = true;
    for (bool quoted = false; true; line++)
    {
        // Spaces are acceptable when quoted, otherwise it's the end of the
        // token and any subsequent text defeats the directory shortcut feature.
        if (*line == ' ' || *line == '\t')
        {
            if (!quoted)
            {
                // Skip trailing whitespace.
                while (*line == ' ' || *line == '\t')
                    line++;
                // Parse fails if input is more than a single token.
                if (*line)
                    return false;
            }
        }

        // Parse succeeds if input is one token.
        if (!*line)
        {
            if (expand)
            {
                str<> tmp;
                if (os::expand_env(out.c_str(), out.length(), tmp))
                    out = tmp.c_str();
            }
            return out.length() > 0;
        }

        switch (*line)
        {
            // These characters defeat the directory shortcut feature.
        case '<':
        case '|':
        case '>':
            return false;

            // These characters are acceptable when quoted.
        case '@':
        case '(':
        case ')':
        case '&':
        case '+':
        case '=':
        case ';':
        case ',':
            if (!quoted)
                return false;
            break;

            // Quotes toggle quote mode.
        case '"':
            first_component = false;
            quoted = !quoted;
            continue;

            // Caret is eaten, unless quoted or the last character.
        case '^':
            first_component = false;
            if (!quoted && line[1])
                continue;
            break;

            // Percent requires expanding environment variables.
        case '%':
            first_component = false;
            expand = true;
            break;

            // These characters end a component.
        case '.':
        case '/':
        case '\\':
            if (first_component)
            {
                // Some commands are special and defeat the directory shortcut
                // feature even if they're legitimately part of an actual path,
                // unless they are quoted.
                static const char* const c_commands[] = { "call", "cd", "chdir", "dir", "echo", "md", "mkdir", "popd", "pushd" };
                for (const char* name : c_commands)
                    if (out.iequals(name))
                        return false;
                first_component = false;
            }
            break;
        }

        out.concat(line, 1);
    }
}

//------------------------------------------------------------------------------
static bool parse_cd_chdir_command(const char*& line, str_base* command=nullptr)
{
    while (*line == ' ' || *line == '\t')
        line++;

    const char* tmp;
    const char* scan = line;
    if (_strnicmp(scan, "cd", 2) == 0)
    {
        scan += 2;
        tmp = "cd";
    }
    else if (_strnicmp(scan, "chdir", 5) == 0)
    {
        scan += 5;
        tmp = "chdir";
    }
    else
        return false;

    bool have_space = false;
    while (*scan == ' ' || *scan == '\t')
    {
        have_space = true;
        scan++;
    }

    if (_strnicmp(scan, "/d", 2) == 0)
    {
        have_space = false;
        scan += 2;
        while (*scan == ' ' || *scan == '\t')
        {
            have_space = true;
            scan++;
        }
    }

    // `cd` and `chdir` require a space before the `-`.
    if (!have_space)
        return false;

    line = scan;
    if (command)
        *command = tmp;
    return true;
}

//------------------------------------------------------------------------------
static bool is_cd_dash(const char* line, bool only_cd_chdir)
{
    str<> command;
    bool cd_chdir = parse_cd_chdir_command(line, &command);

    if (only_cd_chdir)
    {
        str<> alias;
        if (!cd_chdir || os::get_alias(command.c_str(), alias))
            return false;
    }

    if (*(line++) != '-')
        return false;

    while (*line == ' ' || *line == '\t')
        line++;

    return !*line;
}

//------------------------------------------------------------------------------
intercept_result intercept_directory(const char* line, str_base* out, bool only_cd_chdir)
{
    // Check for '-' (etc) to change to previous directory.
    if (is_cd_dash(line, only_cd_chdir))
        return intercept_result::prev_dir;

    if (only_cd_chdir)
        return intercept_result::none;

    // Parse the input for a single token.
    str_moveable tmp;
    if (!parse_line_token(tmp, line))
        return intercept_result::none;

    // If all dots, convert into valid path syntax moving N-1 levels.
    // Examples:
    //  - "..." becomes "..\..\"
    //  - "...." becomes "..\..\..\"
    int32 num_dots = 0;
    for (const char* p = tmp.c_str(); *p; ++p, ++num_dots)
    {
        if (*p != '.')
        {
            if (!path::is_separator(p[0]) || p[1]) // Allow "...\"
                num_dots = -1;
            break;
        }
    }
    if (num_dots >= 2)
    {
        tmp.clear();
        while (num_dots > 1)
        {
            tmp.concat("..\\");
            --num_dots;
        }
    }

    // If the input doesn't end with a separator, don't handle it.  Otherwise
    // it would interfere with launching something found on the PATH but with
    // the same name as a subdirectory of the current working directory.
    if (!tmp.equals("~") && !path::is_separator(tmp.c_str()[tmp.length() - 1]))
    {
        // But allow a special case for "..\.." and "..\..\..", etc.
        const char* p = tmp.c_str();
        while (true)
        {
            if (p[0] != '.' || p[1] != '.')
                return intercept_result::none;
            if (p[2] == '\0')
            {
                tmp.concat("\\");
                break;
            }
            if (!path::is_separator(p[2]))
                return intercept_result::none;
            p += 3;
        }
    }

    path::tilde_expand(tmp);

    if (os::get_path_type(tmp.c_str()) != os::path_type_dir)
        return intercept_result::none;

    // Normalize to system path separator, since `cd /d "/foo/"` fails because
    // the `/d` flag disables `cd` accepting forward slashes in paths.
    if (out)
    {
        path::normalise_separators(tmp);
        out->format(" cd /d \"%s\"", tmp.c_str());
    }

    return intercept_result::chdir;
}
