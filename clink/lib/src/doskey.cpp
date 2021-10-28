// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "doskey.h"
#include "terminal_helpers.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>

#include "terminal/printer.h"

//------------------------------------------------------------------------------
static setting_bool g_enhanced_doskey(
    "doskey.enhanced",
    "Add enhancements to Doskey",
    "Enhanced Doskey adds the expansion of macros that follow '|' and '&'\n"
    "command separators and respects quotes around words when parsing $1...9\n"
    "tags.  Note that these features do not apply to Doskey use in Batch files.\n"
    "\n"
    "WARNING:  Turning this on changes how doskey macros are expanded; some\n"
    "macros may function differently, or may not work at all.",
    false);



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
    unsigned int            length() const;
    unsigned int            trimmed_length() const;
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
unsigned int str_stream::length() const
{
    return static_cast<unsigned int>(m_cursor - m_start);
}

//------------------------------------------------------------------------------
unsigned int str_stream::trimmed_length() const
{
    unsigned int len = length();
    while (len && m_start[len - 1] == ' ')
        len--;
    return len;
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
doskey::doskey(const wchar_t* shell_name)
: m_shell_name(shell_name)
{
}

//------------------------------------------------------------------------------
bool doskey::add_alias(const char* alias, const char* text)
{
    wstr<64> walias(alias);
    wstr<> wtext(text);
    return (AddConsoleAliasW(walias.data(), wtext.data(), m_shell_name.data()) == TRUE);
}

//------------------------------------------------------------------------------
bool doskey::remove_alias(const char* alias)
{
    wstr<64> walias(alias);
    return (AddConsoleAliasW(walias.data(), nullptr, m_shell_name.data()) == TRUE);
}

//------------------------------------------------------------------------------
//#define DEBUG_RESOLVEIMPL
bool doskey::resolve_impl(const str_iter& in, str_stream* out, int* point)
{
    const unsigned int in_len = in.length();

    // Find the alias for which to retrieve text for.
    str_tokeniser tokens(in, " ");
    str_iter token;
    if (!tokens.next(token))
        return false;

    // Find the point range for the alias name.
    int point_arg = -1;
    int point_ofs = -1;
    bool point_arg_star = false;
    int point_ofs_star = -1;
    if (point && out)
    {
        const int delim_len = tokens.peek_delims();
        const int alias_start = out->length();
        const int alias_end = alias_start + token.length() + delim_len;
        if (*point >= alias_start && *point < alias_end)
        {
            // Put point at beginning of whitespace before arg 1.
            point_arg = 0;
            point_ofs = -1;
        }
    }

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
    if (!GetConsoleAliasW(walias.data(), &unused, sizeof(unused), m_shell_name.data()))
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return false;

    // It does. Allocate space and fetch it.
    wstr<4> wtext;
    wtext.reserve(8192, true/*exact*/);
    GetConsoleAliasW(walias.data(), wtext.data(), wtext.size(), m_shell_name.data());
    str<4> text(wtext.c_str());

    // Early out if no output location was provided:  return true because the
    // command is an alias that needs to be expanded (this is unreachable if the
    // GetConsoleAliasW call doesn't find an alias).
    if (out == nullptr)
        return true;

    const int out_len = out->length();
    if (point && *point == out_len)
        point = nullptr; // Point at beginning stays there.

#ifdef DEBUG_RESOLVEIMPL
    int dbg_row = 0;
    str<> tmp;
    if (g_printer)
    {
        dbg_row++;
        tmp.format("\x1b[s\x1b[%dH\x1b[K", dbg_row);
        g_printer->print(tmp.c_str(), tmp.length());
        if (point_arg == 0 && point_ofs < 0)
            g_printer->print("before arg1 \t");
    }
#endif

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

        // Find the point range for the arg.
        if (point)
        {
            const int token_start = out_len + int(desc->ptr - in.get_pointer());
            if (*point >= token_start)
            {
                const int delim_len = tokens.peek_delims();
                const int token_end = token_start + desc->length;
                if (*point < token_end)
                {
                    point_arg = args.size() - 1;
                    point_ofs = *point - token_start;
                }
                else if (*point < token_end + delim_len)
                {
                    point_arg = args.size();
                    point_ofs = -1;
                }
            }
        }
    }

    if (point && args.size())
    {
        if (point_arg == int(args.size()))
            point_arg = -1;
        point_arg_star = true;
        if (*point < out_len + args.front()->ptr - in.get_pointer())
            point_ofs_star = -1;
        else
            point_ofs_star = *point - int(out_len + args.front()->ptr - in.get_pointer());
#ifdef DEBUG_RESOLVEIMPL
        if (g_printer)
        {
            tmp.format("size\t%d\targ begin\t%d\targ end\t%d\tparg\t%d\tin_cmd\t%d\tofs_star %d\t", int(args.size()), int(out_len + args.front()->ptr - in.get_pointer()), int(out_len + args.back()->ptr - in.get_pointer() + args.back()->length), point_arg, point_arg_star, point_ofs_star);
            g_printer->print(tmp.c_str(), tmp.length());
        }
#endif
    }

#ifdef DEBUG_RESOLVEIMPL
    if (g_printer)
    {
        dbg_row++;
        tmp.format("\x1b[%dH\x1b[K", dbg_row);
        g_printer->print(tmp.c_str(), tmp.length());
    }
#endif

    // Expand the alias' text into 'out'.
    bool set_point = !!point;
    str_stream& stream = *out;
    const char* read = text.c_str();
    int last_arg_resolved = -1;
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
        char o = 0;
        switch (c)
        {
        case '$':           o = '$';  break;
        case 'g': case 'G': o = '>';  break;
        case 'l': case 'L': o = '<';  break;
        case 'b': case 'B': o = '|';  break;
        case 't': case 'T': o = '\n'; break;
        }
        if (o)
        {
            if (o == '\n')
                set_point = false; // Only adjust point for the first line.
            stream << o;
            continue;
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

        // Adjust point.
        if (point)
        {
            if (c < 0)
            {
                if (point_arg_star || point_arg >= 0)
                {
#ifdef DEBUG_RESOLVEIMPL
                    if (g_printer)
                    {
                        if (point)
                        {
                            if (point_ofs_star < 0)
                                tmp.format("STAR:\tTlen\t%d\t", stream.trimmed_length());
                            else
                                tmp.format("STAR:\tslen\t%d\tofs\t%d\t", stream.length(), point_ofs_star);
                            g_printer->print(tmp.c_str(), tmp.length());
                        }
                    }
#endif
                    if (point_ofs_star < 0)
                        *point = stream.trimmed_length();
                    else
                        *point = stream.length() + point_ofs_star;
                    point = nullptr;
                }
            }
            else if (c < arg_count)
            {
                if (c == point_arg && point_arg >= 0 && point_ofs >= 0)
                {
#ifdef DEBUG_RESOLVEIMPL
                    if (g_printer && point)
                    {
                        tmp.format("ARG%d:\tslen\t%d\tofs\t%d\t", c + 1, stream.length(), point_ofs);
                        g_printer->print(tmp.c_str(), tmp.length());
                    }
#endif
                    *point = stream.length() + point_ofs;
                    point = nullptr;
                }
                else if (last_arg_resolved + 1 == point_arg && point_arg >= 0 && point_ofs < 0)
                {
#ifdef DEBUG_RESOLVEIMPL
                    if (g_printer && point)
                    {
                        tmp.format("ARG%d:\tTlen\t%d\t\t\t", c + 1, stream.trimmed_length());
                        g_printer->print(tmp.c_str(), tmp.length());
                    }
#endif
                    *point = stream.trimmed_length();
                    point = nullptr;
                }
            }

            last_arg_resolved = c;
        }

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

    // Couldn't figure out where the point belongs?  Put it at the end.
    if (point)
    {
#ifdef DEBUG_RESOLVEIMPL
        if (g_printer)
        {
            tmp.format("END:\tTlen\t%d\t", stream.trimmed_length());
            g_printer->print(tmp.c_str(), tmp.length());
        }
#endif
        *point = max<int>(out_len, stream.trimmed_length());
    }

#ifdef DEBUG_RESOLVEIMPL
    if (g_printer)
        g_printer->print("\x1b[u");
#endif

    return true;
}

//------------------------------------------------------------------------------
//#define DEBUG_RESOLVE
void doskey::resolve(const char* chars, doskey_alias& out, int* point)
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

                if (resolves = resolve_impl(command, nullptr, point))
                    break;
            }

            if (!resolves)
                return;
        }

        // This line will expand aliases so lets do that.
        {
#ifdef DEBUG_RESOLVE
            int dbg_row = 0;
#endif

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
                {
                    first = false;
                }
                else if (command.length() && command.get_pointer()[0] == ' ')
                {
                    stream << command.get_pointer()[0];
                    command.next();
                }

                const int base_len = stream.length();
                int* sub_point = nullptr;
                if (point &&
                    *point >= command.get_pointer() - chars &&
                    *point < int((command.get_pointer() - chars) + command.length()))
                {
                    sub_point = point;
                }

#ifdef DEBUG_RESOLVE
                str<> tmp;
                if (g_printer)
                {
                    dbg_row++;
                    tmp.format("\x1b[s\x1b[%dH\x1b[K", dbg_row);
                    g_printer->print(tmp.c_str(), tmp.length());
                    if (point)
                    {
                        if (sub_point)
                            g_printer->print("sub_point\t");
                        if (!sub_point)
                            g_printer->print("\t\t");
                        tmp.format("begin\t%d\tend\t%d\tbase\t%d\t", int(command.get_pointer() - chars), command.length(), base_len);
                        g_printer->print(tmp.c_str(), tmp.length());
                    }
                }
#endif

                if (!resolve_impl(command, &stream, sub_point))
                    stream << str_stream::range(command);

#ifdef DEBUG_RESOLVE
                if (g_printer)
                {
                    if (point)
                    {
                        tmp.format("point\t%d\t", *point);
                        g_printer->print(tmp.c_str(), tmp.length());
                    }
                    g_printer->print("\x1b[u");
                }
#endif

                if (!sub_point && point && *point >= int((command.get_pointer() - chars) + command.length()))
                {
                    *point -= command.length();
                    *point += stream.length() - base_len;
                }
            }

            // Append any trailing delimiters too.
            while (*last)
                stream << *last++;
        }
    }
    else if (!resolve_impl(str_iter(chars), &stream, point))
        return;

    stream << '\0';

    // Collect the resolve result
    stream.collect(out.m_buffer);
    out.m_cursor = out.m_buffer.c_str();
}
