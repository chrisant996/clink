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
            match_generator_tester(const char* line, const char* lcd, ...);
            match_generator_tester(const str_base& line, const char* lcd, ...);
    void    initialise() {}
    void    run(const char* line, const char* lcd, va_list arg);
    void    shutdown() {}
    T       m_generator;
};

//------------------------------------------------------------------------------
template <class T>
match_generator_tester<T>::match_generator_tester(const char* line, const char* lcd, ...)
{
    va_list arg;
    va_start(arg, lcd);
    run(line, lcd, arg);
    va_end(arg);
};

//------------------------------------------------------------------------------
template <class T>
match_generator_tester<T>::match_generator_tester(const str_base& line, const char* lcd, ...)
{
    va_list arg;
    va_start(arg, lcd);
    run(line.c_str(), lcd, arg);
    va_end(arg);
};

//------------------------------------------------------------------------------
template <class T>
void match_generator_tester<T>::run(const char* line, const char* lcd, va_list arg)
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

    // Check the lowest common denominator of the matches.
    str<> result_lcd;
    result.get_match_lcd(result_lcd);

    if (result.get_match_count() && !result_lcd.equals(lcd))
    {
        printf(" expected: LCD '%s'\n", lcd);
        printf("      got: LCD '%s'\n", result_lcd.c_str());
        REQUIRE(false);
    }

    // Test 'em.
    int match_count = 0;
    va_list arg_iter = arg;
    while (const char* match = va_arg(arg_iter, const char*))
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

    if (match_count != result.get_match_count())
    {
        int i = 0;
        va_list arg_iter = arg;
        while (const char* match = va_arg(arg_iter, const char*))
            printf(" expected: %02d '%s'\n", i++, match);

        for (int i = 0, n = result.get_match_count(); i < n; ++i)
            printf("      got: %02d '%s'\n", i, result.get_match(i));
    }

    REQUIRE(match_count == result.get_match_count());

    shutdown();
}
