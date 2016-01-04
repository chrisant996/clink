// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "file_match_generator.h"
#include "line_state.h"
#include "matches/matches.h"

#include <core/globber.h>
#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
file_match_generator::file_match_generator()
{
}

//------------------------------------------------------------------------------
file_match_generator::~file_match_generator()
{
}

//------------------------------------------------------------------------------
bool file_match_generator::generate(const line_state& line, matches& out)
{
    str<MAX_PATH> buffer;

    // Get the path to match files from, append a slash.
    buffer = line.word;
    path::get_directory(buffer);
    path::append(buffer, "");

    // Glob the files.
    globber::context context = { buffer.c_str(), "*" };
    globber globber(context);

    while (globber.next(buffer))
        out.add_match(buffer.c_str());

    return true;
}
