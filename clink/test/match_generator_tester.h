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

#pragma once

#include <core/str.h>
#include <line_state.h>
#include <stdarg.h>

//------------------------------------------------------------------------------
template <class T>
struct match_generator_tester
{
            match_generator_tester(const char* line, ...);
            match_generator_tester(const str_base& line, ...);
    void    initialise() {}
    void    run(const char* line, va_list arg);
    void    shutdown() {}
    T       m_generator;
};

//------------------------------------------------------------------------------
template <class T>
match_generator_tester<T>::match_generator_tester(const char* line, ...)
{
    va_list arg;
    va_start(arg, line);
    run(line, arg);
    va_end(arg);
};

//------------------------------------------------------------------------------
template <class T>
match_generator_tester<T>::match_generator_tester(const str_base& line, ...)
{
    va_list arg;
    va_start(arg, line);
    run(line.c_str(), arg);
    va_end(arg);
};

//------------------------------------------------------------------------------
template <class T>
void match_generator_tester<T>::run(const char* line, va_list arg)
{
    initialise();

    // Build the line state.
    str<> command = line;
    int start = command.last_of(' ') + 1;
    int end = command.length();
    int cursor = end;
    const char* word = command.c_str() + start;

    // Generate the matches.
    line_state state = { word, command.c_str(), start, end, cursor };
    match_result result = m_generator.generate(state);

    // Test 'em.
    int match_count = 0;
    while (const char* match = va_arg(arg, const char*))
    {
        bool ok = false;
        for (int i = 0, n = result.get_match_count(); i < n; ++i)
        {
            const char* candidate = result.get_match(i);
            if (ok = (strcmp(match, candidate) == 0))
                break;
        }

        if (ok)
            ++match_count;
    }

    REQUIRE(result.get_match_count() == match_count);

    shutdown();
}
