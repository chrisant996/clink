// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "file_match_generator.h"
#include "core/globber.h"
#include "core/path.h"
#include "core/str.h"
#include "line_state.h"
#include "matches/matches.h"

//------------------------------------------------------------------------------
file_match_generator::file_match_generator()
{
}

//------------------------------------------------------------------------------
file_match_generator::~file_match_generator()
{
}

//------------------------------------------------------------------------------
bool file_match_generator::generate(const line_state& line, matches_builder& builder)
{
    str<MAX_PATH> buffer;

    // Get the path to match files from.
    buffer = line.word;
    path::get_directory(buffer);
    path::append(buffer, "");

    // Glob the files.
    globber::context context = { buffer.c_str(), "*" };
    globber globber(context);

    while (globber.next(buffer))
        builder.consider_match(buffer.c_str());

    return true;
}
