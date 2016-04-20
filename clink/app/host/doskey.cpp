// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "doskey.h"

#include <core/base.h>
#include <core/str.h>

//------------------------------------------------------------------------------
doskey::doskey(const char* shell_name)
: m_shell_name(shell_name)
, m_alias_text(nullptr)
, m_alias_next(nullptr)
, m_input(nullptr)
, m_token_count(0)
{
}

//------------------------------------------------------------------------------
doskey::~doskey()
{
    if (m_alias_text != nullptr)
        free(m_alias_text);
}

//------------------------------------------------------------------------------
bool doskey::add_alias(const char* alias, const char* text)
{
    wstr<64> walias(alias);
    wstr<> wtext(text);
    wstr<64> wshell(m_shell_name);
    return (AddConsoleAliasW(walias.data(), wtext.data(), wshell.data()) == TRUE);
}

//------------------------------------------------------------------------------
bool doskey::remove_alias(const char* alias)
{
    wstr<64> walias(alias);
    wstr<64> wshell(m_shell_name);
    return (AddConsoleAliasW(walias.data(), nullptr, wshell.data()) == TRUE);
}

//------------------------------------------------------------------------------
bool doskey::begin(wchar_t* chars, unsigned max_chars)
{
    // Find the alias for which to retrieve text for.
    wchar_t alias[64];
    {
        int i, n;
        int found_word = 0;
        const wchar_t* read = chars;
        for (i = 0, n = min<int>(sizeof_array(alias) - 1, max_chars); i < n && *read; ++i)
        {
            if (!!iswspace(*read) == found_word)
            {
                if (!found_word)
                    found_word = 1;
                else
                    break;
            }

            alias[i] = *read++;
        }

        alias[i] = '\0';
    }

    // Find the alias' text. First check it exists.
    wchar_t wc;
    wstr<64> wshell(m_shell_name);
    if (!GetConsoleAliasW(alias, &wc, 1, wshell.data()))
        return false;

    // It does. Allocate space and fetch it.
    int bytes = max_chars * sizeof(wchar_t);
    m_alias_text = (wchar_t*)malloc(bytes * 2);
    GetConsoleAliasW(alias, m_alias_text, bytes, wshell.data());

    // Copy the input and tokenise it. Lots of pointer aliasing here...
    m_input = m_alias_text + max_chars;
    memcpy(m_input, chars, bytes);

    m_token_count = tokenise(m_input, m_tokens, sizeof_array(m_tokens));

    m_alias_next = m_alias_text;

    // Expand all '$?' codes except those that expand into arguments.
    {
        wchar_t* read = m_alias_text;
        wchar_t* write = read;
        while (*read)
        {
            if (read[0] != '$')
            {
                *write++ = *read++;
                continue;
            }

            ++read;
            switch (*read)
            {
            case '$': *write++ = '$'; break;
            case 'g':
            case 'G': *write++ = '>'; break;
            case 'l':
            case 'L': *write++ = '<'; break;
            case 'b':
            case 'B': *write++ = '|'; break;
            case 't':
            case 'T': *write++ = '\1'; break;

            default:
                *write++ = '$';
                *write++ = *read;
            }

            ++read;
        }

        *write = '\0';
    }

    return next(chars, max_chars);
}

//------------------------------------------------------------------------------
bool doskey::next(wchar_t* chars, unsigned max_chars)
{
    if (m_alias_text == nullptr)
        return false;

    wchar_t* read = m_alias_next;
    if (*read == '\0')
    {
        free(m_alias_text);
        m_alias_text = nullptr;
        return false;
    }

    --max_chars;
    while (*read > '\1' && max_chars)
    {
        wchar_t c = *read++;

        // If this isn't a '$X' code then just copy the character out.
        if (c != '$')
        {
            *chars++ = c;
            --max_chars;
            continue;
        }

        // This is is a '$X' code. All but argument codes have been handled so
        // it is just argument codes to expand now.
        c = *read++;
        if (c >= '1' && c <= '9')   c -= '1' - 1; // -1 as first arg is token 1
        else if (c == '*')          c = 0;
        else if (c > '\1')
        {
            --read;
            *chars++ = '$';
            --max_chars;
        }
        else
            break;

        // 'c' is the index to the argument to insert or -1 if it is all of
        // them. 0th token is alias so arguments start at index 1.
        if (m_token_count > 1)
        {
            wchar_t* insert_from;
            int insert_length = 0;

            if (c == 0 && m_token_count > 1)
            {
                insert_from = m_input + m_tokens[1].start;
                insert_length = min<int>(int(wcslen(insert_from)), max_chars);
            }
            else if (c < m_token_count)
            {
                insert_from = m_input + m_tokens[c].start;
                insert_length = min<int>(m_tokens[c].length, max_chars);
            }

            if (insert_length)
            {
                wcsncpy(chars, insert_from, insert_length);
                max_chars -= insert_length;
                chars += insert_length;
            }
        }
    }

    *chars = '\0';

    // Move m_next on to the next command or the end of the expansion.
    m_alias_next = read;
    while (*m_alias_next > '\1')
        ++m_alias_next;

    if (*m_alias_next == '\1')
        ++m_alias_next;

    return true;
}

//------------------------------------------------------------------------------
int doskey::tokenise(wchar_t* source, token* tokens, int max_tokens)
{
    // The doskey tokenisation (done by conhost.exe on Win7 and in theory
    // available to any console app) is pretty basic. Doesn't take quotes into
    // account. Doesn't skip leading whitespace.

    int i;
    wchar_t* read;

    read = source;
    for (i = 0; i < max_tokens && *read; ++i)
    {
        // Skip whitespace, store the token start.
        while (*read && iswspace(*read))
            ++read;

        tokens[i].start = (short)(read - source);

        // Skip token to next whitespace, store token length.
        while (*read && !iswspace(*read))
            ++read;

        tokens[i].length = (short)(read - source) - tokens[i].start;
    }

    // Don't skip initial whitespace of the first work (the alias key).
    tokens[0].length += tokens[0].start;
    tokens[0].start = 0;
    return i;
}
