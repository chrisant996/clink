// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "file_match_generator.h"
#include "core/globber.h"
#include "core/path.h"
#include "core/str.h"
#include "line_state.h"

//------------------------------------------------------------------------------
file_match_generator::file_match_generator()
{
}

//------------------------------------------------------------------------------
file_match_generator::~file_match_generator()
{
}

//------------------------------------------------------------------------------
match_result file_match_generator::generate(const line_state& line)
{
    globber::context context = { line.word, "*" };
    globber globber(context);

    match_result result;
    str<MAX_PATH> file;
    while (globber.next(file))
        result.add_match(file.c_str());

    return result;
}
