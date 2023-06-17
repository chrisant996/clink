// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "doskey.h"
#include "cmd_tokenisers.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/str_tokeniser.h>
#include <core/debugheap.h>

#include "terminal/printer.h"
#include "terminal/terminal_helpers.h"

//------------------------------------------------------------------------------
setting_bool g_enhanced_doskey(
    "doskey.enhanced",
    "Add enhancements to Doskey",
    "Enhanced Doskey adds the expansion of macros that follow '|' and '&'\n"
    "command separators and respects quotes around words when parsing $1...9\n"
    "tags.  Note that these features do not apply to Doskey use in Batch files.",
    true);



//------------------------------------------------------------------------------
static bool get_alias(const wchar_t* shell_name, str_iter& in, uint32& skipped, str_base& alias, str_base& text, int32& parens, bool relaxed=false)
{
    alias.clear();
    text.clear();

    // Skip leading spaces and parens.
    bool first = true;
    const char* orig = in.get_pointer();
    parens = skip_leading_parens(in, first);
    skipped = uint32(in.get_pointer() - orig);

    // Legacy doskey doesn't allow macros that begin with whitespace.
    const char* start = in.get_pointer();
    if (in.more() && *start == ' ')
    {
        in.reset_pointer(orig);
        return false;
    }

    while (true)
    {
        const int32 c = in.peek();

        if (c == '>')
        {
            in.next();
            if (in.peek() == '&')
                in.next();
            continue;
        }

        if (c == '\0' || c == ' ' || (relaxed && (c == '|' || c == '&')))
        {
            uint32 len = uint32(in.get_pointer() - start);
            len = trim_trailing_parens(start, 0, len, parens);
            alias.concat(start, len);
            break;
        }

        in.next();
    }

    if (alias.empty())
    {
fallback:
        in.reset_pointer(orig);
        if (relaxed || !g_enhanced_doskey.get())
            return false;
        return get_alias(shell_name, in, skipped, alias, text, parens, true);
    }

    // Find the alias' text. First check it exists.
    wchar_t unused;
    wstr<32> walias(alias.c_str());
    if (!GetConsoleAliasW(walias.data(), &unused, sizeof(unused), const_cast<wchar_t*>(shell_name)))
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            goto fallback;

    // It does. Allocate space and fetch it.
    wstr_moveable wtext;
    wtext.reserve(8192, true/*exact*/);
    GetConsoleAliasW(walias.data(), wtext.data(), wtext.size(), const_cast<wchar_t*>(shell_name));
    text = wtext.c_str();

    // Advance the iterator.
    while (in.peek() == ' ')
        in.next();
    return true;
}

//------------------------------------------------------------------------------
static const char* find_command_end(const char* command, int32 len)
{
    bool quote = false;
    str_iter eat(command, len);
    while (eat.more())
    {
        const char *const ptr = eat.get_pointer();
        const int32 c = eat.next();
        if (c == '\"')
        {
            quote = !quote;
            continue;
        }

        if (quote)
            continue;

        if (c == '>' && eat.peek() == '&')
        {
            eat.next();
        }
        else if (c == '^' && eat.more())
        {
            eat.next();
        }
        else if (c == '|' || c == '&')
        {
            // Command separator found.
            return ptr;
        }
    }
    return eat.get_pointer();
}



//------------------------------------------------------------------------------
class str_stream
{
public:
    typedef char            TYPE;

    struct range_desc
    {
        const TYPE* const   ptr;
        uint32              count;
    };

                            str_stream();
                            ~str_stream();
    void                    operator << (TYPE c);
    void                    operator << (const range_desc desc);
    uint32                  length() const;
    uint32                  trimmed_length() const;
    void                    collect(str_impl<TYPE>& out);
    static range_desc       range(TYPE const* ptr, uint32 count);
    static range_desc       range(const str_iter_impl<TYPE>& iter);

private:
    void                    grow(uint32 hint=128);
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
str_stream::~str_stream()
{
    free(m_start);
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

    for (uint32 i = 0; i < desc.count; ++i, ++m_cursor)
        *m_cursor = desc.ptr[i];
}

//------------------------------------------------------------------------------
str_stream::range_desc str_stream::range(const TYPE* ptr, uint32 count)
{
    return { ptr, count };
}

//------------------------------------------------------------------------------
str_stream::range_desc str_stream::range(const str_iter_impl<TYPE>& iter)
{
    return { iter.get_pointer(), iter.length() };
}

//------------------------------------------------------------------------------
uint32 str_stream::length() const
{
    return uint32(m_cursor - m_start);
}

//------------------------------------------------------------------------------
uint32 str_stream::trimmed_length() const
{
    uint32 len = length();
    while (len && m_start[len - 1] == ' ')
        len--;
    return len;
}

//------------------------------------------------------------------------------
void str_stream::collect(str_impl<TYPE>& out)
{
    out.attach(m_start, int32(m_cursor - m_start));
    m_start = m_end = m_cursor = nullptr;
}

//------------------------------------------------------------------------------
void str_stream::grow(uint32 hint)
{
    hint = (hint + 127) & ~127;
    uint32 size = int32(m_end - m_start) + hint;
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
bool doskey::resolve_impl(str_iter& s, str_stream& out, int32* _point)
{
    const int32 out_len = out.length();
    str_iter command = s;
    str_iter in = s;

    // Get alias and macro text.
    str<32> alias;
    str<32> text;
    uint32 skipped;
    int32 parens;
    if (!get_alias(m_shell_name.data(), in, skipped, alias, text, parens))
        return false;
    out << str_stream::range(s.get_pointer(), skipped);

    // Find the point range for the alias name.
    int32 point_arg = -1;
    int32 point_ofs = -1;
    bool point_arg_star = false;
    int32 point_ofs_star = -1;
    if (_point)
    {
        const int32 alias_start = out.length();
        const int32 alias_end = alias_start + int32(in.get_pointer() - s.get_pointer());
        if (*_point >= alias_start && *_point < alias_end)
        {
            // Put point at beginning of whitespace before arg 1.
            point_arg = 0;
            point_ofs = -1;
        }
    }

    // Point at beginning stays there.
    int32* point = (_point && uint32(*_point) <= out.length()) ? nullptr : _point;

#ifdef DEBUG_RESOLVEIMPL
    int32 dbg_row = 0;
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

    // Either split the input at the next command separator, or use the entire
    // input, depending on the doskey.enhanced setting and the macro text.
    bool split = g_enhanced_doskey.get();
    if (split)
    {
        bool quote = false;
        str_iter macro(text.c_str(), text.length());
        while (macro.more())
        {
            const int32 c = macro.next();
            if (c == '\"')
                quote = !quote;
            if (c != '$')
                continue;

            const int32 tag = macro.peek();
            if ((tag == '*') || (tag >= '1' && tag <= '9'))
            {
                // $* or $1..9 exists inside quotes:  don't split.
                // Suppose `ps=powershell "$*"`, then the `|` should be passed
                // to powershell when `ps applet |Format-Table` is used.
                if (quote)
                {
                    split = false;
                    break;
                }
            }
            else if (tag == '$')
            {
                // Skip both chars of '$$' tag.
                macro.next();
            }
        }
    }
    if (split)
    {
        // Restrict to resolve only up to the command separator.
        const char* start = in.get_pointer();
        const char* end = in.get_pointer() + in.length();
        const char* ptr = find_command_end(start, int32(end - start));
        uint32 len = uint32(ptr - in.get_pointer());
        len = trim_trailing_parens(start, 0, len, parens);
        command.truncate(int32(ptr - command.get_pointer()));
        in.truncate(len);

        // Consume the input up to the command separator.
        new (&s) str_iter(ptr, int32(end - ptr));
    }
    else
    {
        // Consume the entire input.
        new (&s) str_iter(in.get_pointer() + in.length(), 0);
    }

    // Don't do point processing if the point is outside the input.
    const bool point_in_command = (point && *point >= out_len && *point <= int32(out_len + command.length()));
    if (point && !point_in_command)
        point = nullptr;

    // Collect the remaining tokens.
    str_tokeniser tokens(in, " ");
    str_iter token;
    if (g_enhanced_doskey.get())
        tokens.add_quote_pair("\"");

    struct arg_desc { const char* ptr; int32 length; };
    fixed_array<arg_desc, 10> args;
    arg_desc* desc;
    while (tokens.next(token) && (desc = args.push_back()))
    {
        desc->ptr = token.get_pointer();
        desc->length = short(token.length());

        // Find the point range for the arg.
        if (point)
        {
            const int32 token_start = out_len + int32(desc->ptr - command.get_pointer());
            if (*point >= token_start)
            {
                const int32 delim_len = tokens.peek_delims();
                const int32 token_end = token_start + desc->length;
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
        if (point_arg == int32(args.size()))
            point_arg = -1;
        point_arg_star = true;
        if (*point < out_len + args.front()->ptr - command.get_pointer())
            point_ofs_star = -1;
        else
            point_ofs_star = *point - int32(out_len + args.front()->ptr - command.get_pointer());
#ifdef DEBUG_RESOLVEIMPL
        if (g_printer)
        {
            tmp.format("size\t%d\targ begin\t%d\targ end\t%d\tparg\t%d\tin_cmd\t%d\tofs_star %d\t", int32(args.size()), int32(out_len + args.front()->ptr - command.get_pointer()), int32(out_len + args.back()->ptr - command.get_pointer() + args.back()->length), point_arg, point_arg_star, point_ofs_star);
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
    str_stream& stream = out;
    const char* read = text.c_str();
    int32 last_arg_resolved = -1;
    for (int32 c = *read; c = *read; ++read)
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

        int32 arg_count = args.size();
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
            const char* end = command.get_pointer() + command.length();
            const char* start = args.front()->ptr;
            stream << str_stream::range(start, int32(end - start));
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
        *point = max<int32>(out_len, stream.trimmed_length());
    }

    // If the point is still ahead, adjust it by the current command delta.
    if (_point && !point_in_command && *_point >= int32(out_len + command.length()))
    {
        *_point -= command.length();
        *_point += out.length() - out_len;
    }

#ifdef DEBUG_RESOLVEIMPL
    if (g_printer)
        g_printer->print("\x1b[u");
#endif

    return true;
}

//------------------------------------------------------------------------------
void doskey::resolve(const char* chars, doskey_alias& out, int32* point)
{
    dbg_ignore_scope(snapshot, "Doskey");

    out.reset();

    str_stream stream;

    bool resolves = false;
    str_iter text(chars, int32(strlen(chars)));
    while (text.more())
    {
        if (resolve_impl(text, stream, point))
        {
            resolves = true;
        }
        else
        {
            if (!g_enhanced_doskey.get())
                break;
            const char* ptr = find_command_end(text.get_pointer(), text.length());
            const int32 len = int32(ptr - text.get_pointer());
            stream << str_stream::range(text.get_pointer(), len);
            new (&text) str_iter(ptr, text.length() - len);
        }

        // Copy delimiters into the output buffer verbatim.
        while (text.more())
        {
            const int32 c = *text.get_pointer();
            if (c != '|' && c != '&')
                break;
            stream << str_stream::range(text.get_pointer(), 1);
            text.next();
        }

        // Ignore 1 space after command separator.  So that resolve_impl can
        // avoid expanding a doskey alias if the command starts with a space.
        // For the first command a single space avoids doskey alias expansion;
        // for subsequent commands it takes two spaces to avoid doskey alias
        // expansion.  This is so `aa & bb & cc` is possible for reability, and
        // ` aa &  bb &  cc` will avoid doskey alias expansion.
        if (text.more() && *text.get_pointer() == ' ')
        {
            stream << *text.get_pointer();
            text.next();
        }
    }

    stream << '\0';

    // Collect the resolve result
    if (resolves)
    {
        stream.collect(out.m_buffer);
        out.m_cursor = out.m_buffer.c_str();
    }
}
