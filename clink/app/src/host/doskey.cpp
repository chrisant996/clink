// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "doskey.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

//------------------------------------------------------------------------------
static setting_bool g_enhanced_doskey(
    "doskey.enhanced",
    "Add enhancements to Doskey",
    "Enhanced Doskey adds the expansion of macros that follow '|' and '&'"
    "command separators and respects quotes around words when parsing $1...9"
    "tags. Note that these features do not apply to Doskey use in Batch files.",
    true);



//------------------------------------------------------------------------------
class wstr_stream
{
public:
                                wstr_stream(wstr_base& x);
    void                        operator << (int c);
    void                        concat(const wchar_t* ptr, int length);

private:
    void                        update();

    wstr_base&                  x;
    const wchar_t* __restrict   start;
    const wchar_t* __restrict   end;
    wchar_t* __restrict         cursor;
};

//------------------------------------------------------------------------------
wstr_stream::wstr_stream(wstr_base& x)
: x(x)
{
    start = x.c_str();
    end = start + x.size() - 1;
    cursor = x.data() + x.length();
}

//------------------------------------------------------------------------------
void wstr_stream::operator << (int c)
{
    if (cursor >= end)
    {
        x.reserve(x.size() + 128);
        update();
    }

    *cursor++ = c;
}

//------------------------------------------------------------------------------
void wstr_stream::concat(const wchar_t* ptr, int length)
{
    *cursor = '\0';
    cursor += length;

    x.concat(ptr, length);

    if (start != x.c_str())
        update();
}

//------------------------------------------------------------------------------
void wstr_stream::update()
{
    int i = int(cursor - start);
    start = x.c_str();
    end = start + x.size() - 1;
    cursor = x.data() + i;
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
bool doskey_alias::next(wstr_base& out)
{
    if (!*this)
        return false;

    out.copy(m_cursor);

    while (*m_cursor++);
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
bool doskey::resolve_impl(const wstr_iter& in, wstr_base& out)
{
    // Find the alias for which to retrieve text for.
    wstr_tokeniser tokens(in, " ");
    wstr_iter token;
    if (!tokens.next(token))
        return false;

    // Legacy doskey doesn't allow macros that begin with whitespace so if the
    // token does it won't ever resolve as an alias.
    const wchar_t* alias_ptr = token.get_pointer();
    if (!g_enhanced_doskey.get() && alias_ptr != in.get_pointer())
        return false;

    wstr<32> alias;
    alias.concat(alias_ptr, token.length());

    // Find the alias' text. First check it exists.
    wchar_t unused;
    wstr<32> wshell(m_shell_name);
    if (!GetConsoleAliasW(alias.data(), &unused, 1, wshell.data()))
        return false;

    // It does. Allocate space and fetch it.
    wstr<4> text;
    text.reserve(512);
    GetConsoleAliasW(alias.data(), text.data(), text.size(), wshell.data());

    // Collect the remaining tokens.
    if (g_enhanced_doskey.get())
        tokens.add_quote_pair("\"");

    struct arg_desc { const wchar_t* ptr; int length; };
    fixed_array<arg_desc, 10> args;
    arg_desc* desc;
    while (tokens.next(token) && (desc = args.push_back()))
    {
        desc->ptr = token.get_pointer();
        desc->length = short(token.length());
    }

    // Expand the alias' text into 'out'.
    wstr_stream stream(out);
    const wchar_t* read = text.c_str();
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
        case 't': case 'T': stream << '\1'; continue;
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
            const wchar_t* end = in.get_pointer() + in.length();
            const wchar_t* start = args.front()->ptr;
            stream.concat(start, int(end - start));
        }
        else if (c < arg_count)
        {
            const arg_desc& desc = args.front()[c];
            stream.concat(desc.ptr, desc.length);
        }
    }

    // Double null-terminated as aliases with $T become and array of commands.
    stream << '\0';
    stream << '\0';

    return true;
}

//------------------------------------------------------------------------------
void doskey::resolve(const wchar_t* chars, doskey_alias& out)
{
    out.reset();

    if (g_enhanced_doskey.get())
    {
        auto& buffer = out.m_buffer;
        const wchar_t* last = chars;
        wstr_iter command;
        wstr_tokeniser commands(chars, "&|");
        commands.add_quote_pair("\"");
        while (commands.next(command))
        {
            // Copy delimiters into the output buffer verbatim.
            if (int delim_length = int(command.get_pointer() - last))
                buffer.concat(last, delim_length);
            last = command.get_pointer() + command.length();

            if (!resolve_impl(command, buffer))
                buffer.concat(command.get_pointer(), command.length());
        }

        // Append any trailing delimiters too.
        if (int delim_length = int(command.get_pointer() - last))
            buffer.concat(last, delim_length);
    }
    else
        resolve_impl(wstr_iter(chars), out.m_buffer);

    out.m_cursor = out.m_buffer.c_str();

    // Convert command delimiters to nulls.
    for (wchar_t* __restrict c = out.m_buffer.data(); *c; ++c)
        if (*c == '\1')
            *c = '\0';
}
