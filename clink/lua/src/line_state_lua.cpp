// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state_lua.h"
#include "lua_state.h"

#include <core/array.h>
#include <lib/line_state.h>

//------------------------------------------------------------------------------
const char* const line_state_lua::c_name = "line_state_lua";
const line_state_lua::method line_state_lua::c_methods[] = {
    { "getline",                &get_line },
    { "getcursor",              &get_cursor },
    { "getcommandoffset",       &get_command_offset },
    { "getcommandwordindex",    &get_command_word_index },
    { "getwordcount",           &get_word_count },
    { "getwordinfo",            &get_word_info },
    { "getword",                &get_word },
    { "getendword",             &get_end_word },
    // UNDOCUMENTED; internal use only.
    { "shift",                  &shift },
    { "join",                   &join },
    {}
};



//------------------------------------------------------------------------------
class line_state_copy _DBGOBJECT
{
public:
                                line_state_copy(const line_state& line);
                                ~line_state_copy() { delete m_line; }
    const line_state*           get_line() const { return m_line; }
    bool                        join(unsigned int index);
private:
    line_state*                 m_line;
    str_moveable                m_buffer;
    std::vector<word>           m_words;
};

//------------------------------------------------------------------------------
line_state_copy::line_state_copy(const line_state& line)
{
    m_buffer.concat(line.get_line(), line.get_length());
    m_words = line.get_words(); // Deep copy.
    m_line = new line_state(m_buffer.c_str(), m_buffer.length(), line.get_cursor(), line.get_command_offset(), m_words);
}

//------------------------------------------------------------------------------
bool line_state_copy::join(unsigned int index)
{
    if (index + 1 >= m_words.size())
        return false;

    auto& this_word = m_words[index];
    const auto& next_word = m_words[index+1];

    // Don't join special words.
    if (this_word.command_word || next_word.command_word)
        return false;
    if (this_word.is_alias || next_word.is_alias)
        return false;
    if (this_word.is_redir_arg || next_word.is_redir_arg)
        return false;

    // Don't join if quoting is different; it would violate the quoting rules.
    if (this_word.quoted != next_word.quoted)
        return false;

    // Join this word, the next word, and the characters in between them.
    if (next_word.length == 0)
        this_word.length = 0; // Special case for end word.
    else
        this_word.length = next_word.offset + next_word.length - this_word.offset;
    this_word.delim = next_word.delim;
    m_words.erase(m_words.begin() + index + 1);

    return true;
}

//------------------------------------------------------------------------------
line_state_copy* make_line_state_copy(const line_state& line)
{
    return new line_state_copy(line);
}



//------------------------------------------------------------------------------
line_state_lua::line_state_lua(const line_state& line)
{
    m_line = &line;
    m_copy = nullptr;
}

//------------------------------------------------------------------------------
line_state_lua::line_state_lua(line_state_copy* copy)
{
    m_line = copy->get_line();
    m_copy = copy;
}

//------------------------------------------------------------------------------
line_state_lua::~line_state_lua()
{
    delete m_copy;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getline
/// -ver:   1.0.0
/// -ret:   string
/// Returns the current line in its entirety.
int line_state_lua::get_line(lua_State* state)
{
    lua_pushlstring(state, m_line->get_line(), m_line->get_length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getcursor
/// -ver:   1.0.0
/// -ret:   integer
/// Returns the position of the cursor.
int line_state_lua::get_cursor(lua_State* state)
{
    lua_pushinteger(state, m_line->get_cursor() + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getcommandoffset
/// -ver:   1.0.0
/// -ret:   integer
/// Returns the offset to the start of the delimited command in the line that's
/// being effectively edited. Note that this may not be the offset of the first
/// command of the line unquoted as whitespace isn't considered for words.
/// -show:  -- Given the following line; abc&123
/// -show:  -- where commands are separated by & symbols.
/// -show:  line_state:getcommandoffset() == 5
///
/// The command offset points to the beginning of the command, but that might be
/// a space character.  Two spaces after a command separator is like one space
/// at the beginning of a line; it disables doskey alias expansion.  So, if the
/// command offset points at a space, then you know the first word will not be
/// treated as a doskey alias.
/// -show:  -- Given the following line; abc&  123
/// -show:  -- where commands are separated by & symbols.
/// -show:  line_state:getcommandoffset() == 6
int line_state_lua::get_command_offset(lua_State* state)
{
    unsigned int offset = m_line->get_command_offset();
    if (m_shift)
    {
        const auto& words = m_line->get_words();
        const auto& w = words[m_shift - 1];
        offset = w.offset + w.length;
        const char* line = m_line->get_line();
        while (line[offset] != ' ' && line[offset] != '\t' && offset < words[m_shift].offset)
            offset++;
    }

    lua_pushinteger(state, offset + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getcommandwordindex
/// -ver:   1.2.27
/// -ret:   integer
/// Returns the index of the command word. Usually the index is 1, but if a
/// redirection symbol occurs before the command name then the index can be
/// greater than 1.
/// -show:  -- Given the following line; >x abc
/// -show:  -- the first word is "x" and is an argument to the redirection symbol,
/// -show:  -- and the second word is "abc" and is the command word.
/// -show:  line_state:getcommandwordindex() == 2
int line_state_lua::get_command_word_index(lua_State* state)
{
    unsigned int index = m_line->get_command_word_index();
    if (m_shift)
    {
        const auto& words = m_line->get_words();
        const unsigned int count = m_line->get_word_count();
        while (index < count && words[index].is_redir_arg)
            index++;
    }

    lua_pushinteger(state, index + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getwordcount
/// -ver:   1.0.0
/// -ret:   integer
/// Returns the number of words in the current line.
int line_state_lua::get_word_count(lua_State* state)
{
    lua_pushinteger(state, m_line->get_word_count() - m_shift);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getwordinfo
/// -ver:   1.0.0
/// -arg:   index:integer
/// -ret:   table
/// Returns a table of information about the Nth word in the line.
///
/// Note:  The length refers to the substring in the line; it omits leading and
/// trailing quotes, but <em><strong>includes</strong></em> embedded quotes.
/// <a href="#line_state:getword">line_state:getword()</a> conveniently strips
/// embedded quotes to help generators naturally complete <code>"foo\"ba</code>
/// to <code>"foo\bar"</code>.
///
/// The table returned has the following scheme:
/// -show:  local t = line_state:getwordinfo(word_index)
/// -show:  -- t.offset     [integer] Offset where the word starts in the line_state:getline() string.
/// -show:  -- t.length     [integer] Length of the word (includes embedded quotes).
/// -show:  -- t.quoted     [boolean] Indicates whether the word is quoted.
/// -show:  -- t.delim      [string] The delimiter character, or an empty string.
/// -show:  -- t.alias      [boolean | nil] true if the word is a doskey alias, otherwise nil.
/// -show:  -- t.redir      [boolean | nil] true if the word is a redirection arg, otherwise nil.
int line_state_lua::get_word_info(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    const std::vector<word>& words = m_line->get_words();
    unsigned int index = int(lua_tointeger(state, 1)) - 1 + m_shift;
    if (index >= words.size())
        return 0;

    const word& word = words[index];

    lua_createtable(state, 0, 6);

    lua_pushliteral(state, "offset");
    lua_pushinteger(state, word.offset + 1);
    lua_rawset(state, -3);

    lua_pushliteral(state, "length");
    lua_pushinteger(state, word.length);
    lua_rawset(state, -3);

    lua_pushliteral(state, "quoted");
    lua_pushboolean(state, word.quoted);
    lua_rawset(state, -3);

    char delim[2] = { char(word.delim) };
    lua_pushliteral(state, "delim");
    lua_pushstring(state, delim);
    lua_rawset(state, -3);

    if (word.is_alias)
    {
        lua_pushliteral(state, "alias");
        lua_pushboolean(state, true);
        lua_rawset(state, -3);
    }

    if (word.is_redir_arg)
    {
        lua_pushliteral(state, "redir");
        lua_pushboolean(state, true);
        lua_rawset(state, -3);
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getword
/// -ver:   1.0.0
/// -arg:   index:integer
/// -ret:   string
/// Returns the word of the line at <span class="arg">index</span>.
///
/// Note:  The returned word omits any quotes.  This helps generators naturally
/// complete <code>"foo\"ba</code> to <code>"foo\bar"</code>.  The raw word
/// including quotes can be obtained using the <code>offset</code> and
/// <code>length</code> fields from
/// <a href="#line_state:getwordinfo">line_state:getwordinfo()</a> to extract a
/// substring from the line returned by
/// <a href="#line_state:getline">line_state:getline()</a>.
int line_state_lua::get_word(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    str<32> word;
    unsigned int index = int(lua_tointeger(state, 1)) - 1;
    m_line->get_word(m_shift + index, word);
    lua_pushlstring(state, word.c_str(), word.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getendword
/// -ver:   1.0.0
/// -ret:   string
/// Returns the last word of the line. This is the word that matches are being
/// generated for.
///
/// Note:  The returned word omits any quotes.  This helps generators naturally
/// complete <code>"foo\"ba</code> to <code>"foo\bar"</code>.  The raw word
/// including quotes can be obtained using the <code>offset</code> and
/// <code>length</code> fields from
/// <a href="#line_state:getwordinfo">line_state:getwordinfo()</a> to extract a
/// substring from the line returned by
/// <a href="#line_state:getline">line_state:getline()</a>.
/// -show:  line_state:getword(line_state:getwordcount()) == line_state:getendword()
int line_state_lua::get_end_word(lua_State* state)
{
    str<32> word;
    m_line->get_end_word(word);
    lua_pushlstring(state, word.c_str(), word.length());
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int line_state_lua::shift(lua_State* state)
{
    unsigned int num = optinteger(state, 1, 0);

    if (num > 0)
    {
        num -= 1;
        if (num > m_line->get_word_count() || m_shift + num > m_line->get_word_count())
            num = m_line->get_word_count() - m_shift;
        if (!num)
            return 0;

        m_shift += num;
    }

    lua_pushinteger(state, m_shift);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int line_state_lua::join(lua_State* state)
{
    if (!lua_isnumber(state, 1))
        return 0;

    unsigned int index = m_shift + int(lua_tointeger(state, 1));
    if (index < 1 || index + 1 > m_line->get_word_count())
    {
nope:
        lua_pushboolean(state, false);
        return 1;
    }

    --index;

    // Make a writable copy, if necessary.
    if (!m_copy)
    {
        m_copy = new line_state_copy(*m_line);
        m_line = m_copy->get_line();
    }

    // Join with next word.
    if (!m_copy->join(index))
        goto nope;

    lua_pushboolean(state, true);
    return 1;
}
