// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "doskey.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>

//------------------------------------------------------------------------------
static setting_bool g_enhanced_doskey(
    "doskey.enhanced",
    "Add enhancements to Doskey",
    "Enhanced Doskey adds the expansion of macros that follow '|' and '&'\n"
    "command separators and respects quotes around words when parsing $1...9\n"
    "tags. Note that these features do not apply to Doskey use in Batch files.",
    true);



//------------------------------------------------------------------------------
class str_stream
{
public:
    typedef char            TYPE;

    struct range_desc
    {
        const TYPE* const   ptr;
        unsigned int        count;
    };

                            str_stream();
    void                    operator << (TYPE c);
    void                    operator << (const range_desc desc);
    void                    collect(str_impl<TYPE>& out);
    static range_desc       range(TYPE const* ptr, unsigned int count);
    static range_desc       range(const str_iter_impl<TYPE>& iter);

private:
    void                    grow(unsigned int hint=128);
    char* __restrict        m_start;
    char* __restrict        m_end;
    char* __restrict        m_cursor;
};

//------------------------------------------------------------------------------
str_stream::str_stream()
: m_start(nullptr)
, m_end(nullptr)
, m_cursor(nullptr)
{
}

//------------------------------------------------------------------------------
void str_stream::operator << (TYPE c)
{
    if (m_cursor >= m_end)
        grow();

    *m_cursor++ = c;
}

//------------------------------------------------------------------------------
void str_stream::operator << (const range_desc desc)
{
    if (m_cursor + desc.count >= m_end)
        grow(desc.count);

    for (unsigned int i = 0; i < desc.count; ++i, ++m_cursor)
        *m_cursor = desc.ptr[i];
}

//------------------------------------------------------------------------------
str_stream::range_desc str_stream::range(const TYPE* ptr, unsigned int count)
{
    return { ptr, count };
}

//------------------------------------------------------------------------------
str_stream::range_desc str_stream::range(const str_iter_impl<TYPE>& iter)
{
    return { iter.get_pointer(), iter.length() };
}

//------------------------------------------------------------------------------
void str_stream::collect(str_impl<TYPE>& out)
{
    out.attach(m_start, int(m_cursor - m_start));
    m_start = m_end = m_cursor = nullptr;
}

//------------------------------------------------------------------------------
void str_stream::grow(unsigned int hint)
{
    hint = (hint + 127) & ~127;
    unsigned int size = int(m_end - m_start) + hint;
    TYPE* next = (TYPE*)realloc(m_start, size * sizeof(TYPE));
    m_cursor = next + (m_cursor - m_start);
    m_end = next + size;
    m_start = next;
}



//------------------------------------------------------------------------------
doskey_alias::doskey_alias()
{
    m_cursor = m_buffer.c_str();
}

//------------------------------------------------------------------------------
void doskey_alias::reset()
{
    m_buffer.clear();
    m_cursor = m_buffer.c_str();
}

//------------------------------------------------------------------------------
bool doskey_alias::next(str_base& out)
{
    if (!*m_cursor)
        return false;

    out.clear();
    while (*m_cursor)
    {
        char c = *m_cursor++;
        if (c == '\n')
            break;
        out.concat(&c, 1);
    }

    return true;
}

//------------------------------------------------------------------------------
doskey_alias::operator bool () const
{
    return (*m_cursor != 0);
}



//------------------------------------------------------------------------------
doskey::doskey(const char* shell_name)
: m_shell_name(shell_name)
{
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
bool doskey::resolve_impl(const str_iter& in, str_stream* out)
{
    // Find the alias for which to retrieve text for.
    str_tokeniser tokens(in, " ");
    str_iter token;
    if (!tokens.next(token))
        return false;

    // Legacy doskey doesn't allow macros that begin with whitespace so if the
    // token does it won't ever resolve as an alias.
    const char* alias_ptr = token.get_pointer();
    if (alias_ptr != in.get_pointer())
        return false;

    str<32> alias;
    alias.concat(alias_ptr, token.length());
    wstr<32> walias(alias.c_str());

    // Find the alias' text. First check it exists.
    wchar_t unused;
    wstr<32> wshell(m_shell_name);
    if (!GetConsoleAliasW(walias.data(), &unused, sizeof(unused), wshell.data()))
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return false;

    // It does. Allocate space and fetch it.
    wstr<4> wtext;
    wtext.reserve(8192, true/*exact*/);
    GetConsoleAliasW(walias.data(), wtext.data(), wtext.size(), wshell.data());
    str<4> text(wtext.c_str());

    // Early out if no output location was provided:  return true because the
    // command is an alias that needs to be expanded (this is unreachable if the
    // GetConsoleAliasW call doesn't find an alias).
    if (out == nullptr)
        return true;

    // Collect the remaining tokens.
    if (g_enhanced_doskey.get())
        tokens.add_quote_pair("\"");

    struct arg_desc { const char* ptr; int length; };
    fixed_array<arg_desc, 10> args;
    arg_desc* desc;
    while (tokens.next(token) && (desc = args.push_back()))
    {
        desc->ptr = token.get_pointer();
        desc->length = short(token.length());
    }

    // Expand the alias' text into 'out'.
    str_stream& stream = *out;
    const char* read = text.c_str();
    for (int c = *read; c = *read; ++read)
    {
        if (c != '$')
        {
            stream << c;
            continue;
        }

        c = *++read;
        if (!c)
            break;

        // Convert $x tags.
        switch (c)
        {
        case '$':           stream << '$';  continue;
        case 'g': case 'G': stream << '>';  continue;
        case 'l': case 'L': stream << '<';  continue;
        case 'b': case 'B': stream << '|';  continue;
        case 't': case 'T': stream << '\n'; continue;
        }

        // Unknown tag? Perhaps it is a argument one?
        if (unsigned(c - '1') < 9)  c -= '1';
        else if (c == '*')          c = -1;
        else
        {
            stream << '$';
            stream << c;
            continue;
        }

        int arg_count = args.size();
        if (!arg_count)
            continue;

        // 'c' is now the arg index or -1 if it is all of them to be inserted.
        if (c < 0)
        {
            const char* end = in.get_pointer() + in.length();
            const char* start = args.front()->ptr;
            stream << str_stream::range(start, int(end - start));
        }
        else if (c < arg_count)
        {
            const arg_desc& desc = args.front()[c];
            stream << str_stream::range(desc.ptr, desc.length);
        }
    }

    return true;
}

//------------------------------------------------------------------------------
void doskey::resolve(const char* chars, doskey_alias& out)
{
    out.reset();

    str_stream stream;
    if (g_enhanced_doskey.get())
    {
        str_iter command;

        // Coarse check to see if there's any aliases to resolve
        {
            bool first = true;
            bool resolves = false;
            str_tokeniser commands(chars, "&|");
            commands.add_quote_pair("\"");
            while (commands.next(command))
            {
                // Ignore 1 space after command separator.  For symmetry.  See
                // loop below for details.
                if (first)
                    first = false;
                else if (command.length() && command.get_pointer()[0] == ' ')
                    command.next();

                if (resolves = resolve_impl(command, nullptr))
                    break;
            }

            if (!resolves)
                return;
        }

        // This line will expand aliases so lets do that.
        {
            bool first = true;
            const char* last = chars;
            str_tokeniser commands(chars, "&|");
            commands.add_quote_pair("\"");
            while (commands.next(command))
            {
                // Copy delimiters into the output buffer verbatim.
                if (int delim_length = int(command.get_pointer() - last))
                    stream << str_stream::range(last, delim_length);
                last = command.get_pointer() + command.length();

                // Ignore 1 space after command separator.  So that resolve_impl
                // can avoid expanding a doskey alias if the command starts with
                // a space.  For the first command a single space avoids doskey
                // alias expansion; for subsequent commands it takes two spaces
                // to avoid doskey alias expansion.  This is so `aa & bb & cc`
                // is possible for reability, and ` aa &  bb &  cc` will avoid
                // doskey alias expansion.
                if (first)
                    first = false;
                else if (command.length() && command.get_pointer()[0] == ' ')
                    command.next();

                if (!resolve_impl(command, &stream))
                    stream << str_stream::range(command);
            }

            // Append any trailing delimiters too.
            while (*last)
                stream << *last++;
        }
    }
    else if (!resolve_impl(str_iter(chars), &stream))
        return;

    stream << '\0';

    // Collect the resolve result
    stream.collect(out.m_buffer);
    out.m_cursor = out.m_buffer.c_str();
}
