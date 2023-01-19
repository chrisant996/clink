// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/path.h>
#include <core/str_compare.h>
#include <core/str_iter.h>

class str_base;

namespace path
{

//------------------------------------------------------------------------------
enum star_matches_everything { no, yes, at_end };

//------------------------------------------------------------------------------
template <class T, int MODE, bool fuzzy_accents>
bool match_char_impl(int pc, int fc)
{
    if (MODE > 0)
    {
        pc = (pc > 0xffff) ? pc : int(uintptr_t(CharLowerW(LPWSTR(uintptr_t(pc)))));
        fc = (fc > 0xffff) ? fc : int(uintptr_t(CharLowerW(LPWSTR(uintptr_t(fc)))));
    }

    if (MODE > 1)
    {
        pc = (pc == '-') ? '_' : pc;
        fc = (fc == '-') ? '_' : fc;
    }

    if (pc == '\\') pc = '/';
    if (fc == '\\') fc = '/';

    if (pc != fc)
    {
        if (!fuzzy_accents)
            return false;
        pc = normalize_accent(pc);
        fc = normalize_accent(fc);
        if (pc != fc)
            return false;
    }

    return true;
}

//------------------------------------------------------------------------------
template <class T, int MODE, bool fuzzy_accents>
bool match_wild_impl(const str_iter_impl<T>& _pattern, const str_iter_impl<T>& _file, bool dot_prefix=false, star_matches_everything match_everything=no)
{
    str_iter_impl<T> pattern(_pattern);
    str_iter_impl<T> file(_file);

    unsigned depth = 0;
    const T* pattern_stack[10];
    const T* file_stack[10];
    bool path_component_stack[10];

    const T* final_pattern_component = nullptr;
    const T* final_file_component = nullptr;
    bool has_final_wildcard = false;

    bool start_of_path_component = true;

    while (true)
    {
        int c = pattern.peek();
        int d = file.peek();
        if (!c)
        {
            // Consumed pattern, so it's a match iff file was consumed.
            if (!d)
                return true;
back_track:
            if (depth)
            {
                // Backtrack.
                depth--;
                pattern.reset_pointer(pattern_stack[depth]);
                file.reset_pointer(file_stack[depth]);
                start_of_path_component = path_component_stack[depth];
                continue;
            }
            return false;
        }

        bool symbol_matched = false;
        switch (c)
        {
        case '?':
            // Any 1 character (or missing character), except slashes.
            if (path::is_separator(d))
                break;
            if (d)
                file.next();
            pattern.next();
            symbol_matched = true;
            break;
        case '*': {
            const T* push_pattern = pattern.get_pointer();
            while (c == '*' || c == '?')
            {
                pattern.next();
                c = pattern.peek();
            }
            if (c == '\0' && match_everything >= yes)
                return true;
            if (c != '?' &&
                c != '*')
            {
                // Iterate until file char matches pattern char after wildcard.
                const T* push_scout = file.get_pointer();
                while (d &&
                       (match_everything == yes || !path::is_separator(d)) &&
                       !match_char_impl<T,MODE,fuzzy_accents>(d, c))
                {
                    file.next();
                    d = file.peek();
                }
                if (!match_char_impl<T,MODE,fuzzy_accents>(d, c))
                {
                    file.reset_pointer(push_scout);
                    break; // Next character has no possible match ahead.
                }
                file.next();
                pattern.next();
            }
            if (match_everything != yes && path::is_separator(d))
            {
                // Wildcards don't match past a path separator.
                depth = 0;
            }
            else
            {
                if (depth == _countof(pattern_stack))
                    return false; // Stack overflow.
                pattern_stack[depth] = push_pattern;
                file_stack[depth] = file.get_pointer();
                path_component_stack[depth] = start_of_path_component;
                depth++;
            }
            symbol_matched = true;
            break; }
        default: {
            // Single character.
            if (!d)
                break;
            if (path::is_separator(d))
                start_of_path_component = true;
            else if (dot_prefix && d == '.' && start_of_path_component)
            {
                if (!final_file_component)
                {
                    int x;
                    for (str_iter tmp(_pattern); x = tmp.peek(); tmp.next())
                        if (path::is_separator(x))
                        {
                            final_pattern_component = tmp.get_pointer() + 1;
                            has_final_wildcard = false;
                        }
                        else if (x == '?' || x == '*')
                        {
                            has_final_wildcard = true;
                        }
                    for (str_iter tmp(_file); x = tmp.peek(); tmp.next())
                        if (path::is_separator(x))
                            final_file_component = tmp.get_pointer() + 1;
                    if (!final_pattern_component)
                        final_pattern_component = _pattern.get_pointer();
                    if (!final_file_component)
                        final_file_component = _file.get_pointer();
                }
                if (has_final_wildcard &&
                    file.get_pointer() == final_file_component &&
                    pattern.get_pointer() == final_pattern_component &&
                    c != '.')
                {
                    while (d == '.')
                    {
                        file.next();
                        d = file.peek();
                    }
                }
            }
            if (!match_char_impl<T,MODE,fuzzy_accents>(d, c))
                break;
            pattern.next();
            file.next();
            symbol_matched = true;
            // Advance past path separators (consider "\\\\" and "\" equal).
            assert(path::is_separator(c) == path::is_separator(d));
            if (path::is_separator(c))
            {
                while (path::is_separator(pattern.peek()))
                    pattern.next();
                while (path::is_separator(file.peek()))
                    file.next();
            }
            break; }
        }

        if (!symbol_matched)
            goto back_track;
    }
}

//------------------------------------------------------------------------------
template <class T>
bool match_wild(const str_iter_impl<T>& pattern, const str_iter_impl<T>& file, bool dot_prefix=false, star_matches_everything match_everything=no)
{
    bool fuzzy_accents = str_compare_scope::current_fuzzy_accents();
    switch (str_compare_scope::current())
    {
    case str_compare_scope::relaxed:
        if (fuzzy_accents)  return match_wild_impl<T, 2, true>(pattern, file, dot_prefix, match_everything);
        else                return match_wild_impl<T, 2, false>(pattern, file, dot_prefix, match_everything);
    case str_compare_scope::caseless:
        if (fuzzy_accents)  return match_wild_impl<T, 1, true>(pattern, file, dot_prefix, match_everything);
        else                return match_wild_impl<T, 1, false>(pattern, file, dot_prefix, match_everything);
    default:
        if (fuzzy_accents)  return match_wild_impl<T, 0, true>(pattern, file, dot_prefix, match_everything);
        else                return match_wild_impl<T, 0, false>(pattern, file, dot_prefix, match_everything);
    }
}

//------------------------------------------------------------------------------
template <class T>
bool match_wild(const T* pattern, const T* file, bool dot_prefix=false, star_matches_everything match_everything=no)
{
    str_iter_impl<T> pattern_iter(pattern);
    str_iter_impl<T> file_iter(file);
    return match_wild(pattern_iter, file_iter, dot_prefix, match_everything);
}

//------------------------------------------------------------------------------
template <class T>
bool match_wild(const str_impl<T>& pattern, const str_impl<T>& file, bool dot_prefix=false, star_matches_everything match_everything=no)
{
    str_iter_impl<T> pattern_iter(pattern);
    str_iter_impl<T> file_iter(file);
    return match_wild(pattern_iter, file_iter, dot_prefix, match_everything);
}

}; // namespace path
