// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

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
    match_result result;
    m_generator.generate(state, result);

    // It's possible that we're not expecting any matches...
    va_list arg_iter = arg;
    const char* lcd = va_arg(arg_iter, const char*);
    if (lcd == nullptr)
    {
        REQUIRE(result.get_match_count() == 0);
        return;
    }

    // ... nope, there should be matches.
    REQUIRE(result.get_match_count() > 0);

    // Check the lowest common denominator of the matches.
    str<> result_lcd;
    result.get_match_lcd(result_lcd);

    if (!result_lcd.equals(lcd))
    {
        printf(" expected: LCD '%s'\n", lcd);
        printf("      got: LCD '%s'\n", result_lcd.c_str());
        REQUIRE(false);
    }

    // Passing no matches is a shortcut to say that we're only expecting one
    // match and that lcd == match[0]. It's to save stating the same thing twice.
    va_list arg_look_ahead = arg_iter;
    const char* match = va_arg(arg_look_ahead, const char*);
    if (match == nullptr)
        arg_iter = arg;

    // Test we've got the matches we expected.
    int arg_count = 0;
    int match_count = 0;
    while (match = va_arg(arg_iter, const char*))
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
