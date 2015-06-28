/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"
#include "file_match_generator.h"
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
    match_result matches;
    matches.add_match(line.word);

    str<MAX_PATH> wildcard;
    wildcard << line.word << "*";

    WIN32_FIND_DATA fd;
    HANDLE find = FindFirstFile(wildcard, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return matches;

    str<MAX_PATH> root;
    path::get_directory(wildcard, root);
    BOOL ok = TRUE;
    while (ok)
    {
        str<MAX_PATH> result;
        path::join(root, fd.cFileName, result);
        matches.add_match(result);

        ok = FindNextFile(find, &fd);
    }

    return matches;
}
