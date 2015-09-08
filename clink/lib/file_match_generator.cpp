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
void file_match_generator::generate(const line_state& line, match_result& result)
{
    str<MAX_PATH> clean_word = line.word;
    path::clean(clean_word);

    match_result_builder builder(result, clean_word.c_str());

    str<MAX_PATH> word_root = line.word;
    path::get_directory(word_root);
    if (word_root.length())
        word_root << "\\";

    globber::context context = { word_root.c_str(), "*" };
    globber globber(context);

    str<MAX_PATH> file;
    while (globber.next(file))
        builder << file.c_str();
}
