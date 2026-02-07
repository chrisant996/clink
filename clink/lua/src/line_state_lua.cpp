// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_state_lua.h"
#include "lua_state.h"

#include <core/array.h>
#include <lib/line_state.h>
#include <lib/cmd_tokenisers.h>

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
    { "getendwordoffset",       &get_end_word_offset },
    { "getrangeoffset",         &get_range_offset },
    { "getrangelength",         &get_range_length },
    // UNDOCUMENTED; internal use only.
    { "_shift",                 &shift },
    { "_reset_shift",           &reset_shift },
    { "_break_word",            &break_word },
    { "_unbreak_word",          &unbreak_word },
    { "_set_alias",             &set_alias },
    { "_overwrite_from",        &overwrite_from },
    { "_test_cmd_builtin",      &test_cmd_builtin },
    {}
};



//------------------------------------------------------------------------------
class line_state_copy
#ifdef USE_DEBUG_OBJECT
: public object
#endif
{
public:
                                line_state_copy(const line_state& line);
                                ~line_state_copy() { delete m_line; }
    const line_state*           get_line() const { return m_line; }
    void                        break_word(uint32 index, uint32 len);
    bool                        unbreak_word(uint32 index, uint32 len);
    void                        set_alias(uint32 index, bool value);
    void                        test_cmd_builtin(uint32 index);
    bool                        is_tested_cmd_builtin() const { return m_tested_cmd_builtin; }
private:
    line_state*                 m_line;
    str_moveable                m_buffer;
    words                       m_words;
    bool                        m_tested_cmd_builtin = false;
};

//------------------------------------------------------------------------------
line_state_copy::line_state_copy(const line_state& line)
{
    m_buffer.concat(line.get_line(), line.get_length());
    m_words = line.get_words(); // Deep copy.
    m_line = new line_state(m_buffer.c_str(), m_buffer.length(), line.get_cursor(), line.get_words_limit(), line.get_command_offset(), line.get_range_offset(), line.get_range_length(), m_words);
}

//------------------------------------------------------------------------------
line_state_copy* make_line_state_copy(const line_state& line)
{
    return new line_state_copy(line);
}

//------------------------------------------------------------------------------
void line_state_copy::break_word(uint32 index, uint32 len)
{
    assert(index < m_words.size());
    assert(len > 0 && len < m_words[index].length);

    word next = m_words[index];
    next.offset += len;
    next.length -= len;
    next.command_word = false;
    assert(!next.is_cmd_command);
    assert(!next.is_alias);
    assert(!next.is_redir_arg);
    assert(!next.is_merged_away);
    assert(!next.quoted);

    word& word = m_words[index];
    word.length = len;
    word.delim = m_buffer.c_str()[word.offset + len];

    m_words.insert(m_words.begin() + index + 1, std::move(next));
}

//------------------------------------------------------------------------------
bool line_state_copy::unbreak_word(uint32 index, uint32 len)
{
    assert(index < m_words.size());
    assert(len > m_words[index].length);
    assert(m_words[index].offset + len <= m_line->get_length());
    assertimplies(index + 1 < m_words.size(), m_words[index].offset + len <= m_words[index + 1].offset);

    m_words[index].length = len;

    const bool into_next = (index + 1 < m_words.size() && m_words[index].offset + len == m_words[index + 1].offset);
    if (into_next)
    {
        assert(index + 1 < m_words.size());
        assert(m_words[index].offset + len == m_words[index + 1].offset);
        assert(!m_words[index + 1].quoted);

        m_words[index + 1].length += m_words[index + 1].offset - m_words[index].offset;
        m_words[index + 1].offset = m_words[index].offset;
        m_words[index].length = 0;
        m_words[index].is_merged_away = true;
    }

    return into_next;
}

//------------------------------------------------------------------------------
void line_state_copy::set_alias(uint32 index, bool value)
{
    assert(index < m_words.size());

    auto& word = m_words[index];

    assert(!word.is_cmd_command);
    assert(!word.is_alias);
    assert(!word.is_redir_arg);
    assert(!word.is_merged_away);
    assert(!word.quoted);

    word.is_alias = value;
    word.command_word = true;
}

//------------------------------------------------------------------------------
void line_state_copy::test_cmd_builtin(uint32 index)
{
    if (m_tested_cmd_builtin)
        return;
    m_tested_cmd_builtin = true;

    assert(index < m_words.size());
    m_words[index].is_cmd_command = true;

    if (index + 1 >= m_words.size())
        return;
    if (m_words[index].offset + m_words[index].length != m_words[index + 1].offset)
        return;
    if (m_words[index + 1].length < 1)
        return;
    if (m_words[index + 1].offset >= m_buffer.length())
        return;
    if (m_buffer.c_str()[m_words[index + 1].offset] != '.')
        return;

    // Is it something like "echo.txt" when a file by that name exists?
    str<> tmp;
    bool ready;
    tmp.concat(m_buffer.c_str() + m_words[index].offset, m_words[index].length + m_words[index + 1].length);
    if (recognize_command(nullptr, tmp.c_str(), false, ready, nullptr) == recognition::executable)
    {
        m_words[index].length += m_words[index + 1].length;
        m_words[index].is_cmd_command = false;
        m_words.erase(m_words.begin() + index + 1);
    }
}



//------------------------------------------------------------------------------
line_state_lua::line_state_lua(const line_state& line)
{
    m_line = &line;
    m_copy = nullptr;
}

//------------------------------------------------------------------------------
line_state_lua::line_state_lua(line_state_copy* copy, uint32 shift)
{
    m_line = copy->get_line();
    m_copy = copy;
    m_shift = shift;
    m_tested_cmd_builtin = copy->is_tested_cmd_builtin();
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
int32 line_state_lua::get_line(lua_State* state)
{
    lua_pushlstring(state, m_line->get_line(), m_line->get_length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getcursor
/// -ver:   1.0.0
/// -ret:   integer
/// Returns the position of the cursor.
int32 line_state_lua::get_cursor(lua_State* state)
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
/// -show:  -- Given the following line;  abc&123
/// -show:  --                                ^
/// -show:  -- In the line_state for the second command;
/// -show:  line_state:getcommandoffset() == 5
///
/// The command offset points to the beginning of the command, but that might be
/// a space character.  Two spaces after a command separator is like one space
/// at the beginning of a line; it disables doskey alias expansion.  So, if the
/// command offset points at a space, then you know the first word will not be
/// treated as a doskey alias.
/// -show:  -- Given the following line;  abc&  123
/// -show:  --                                 ^
/// -show:  -- In the line_state for the second command;
/// -show:  line_state:getcommandoffset() == 6
int32 line_state_lua::get_command_offset(lua_State* state)
{
    uint32 offset = m_line->get_command_offset();
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
/// -show:  -- Given the following line;  >x abc
/// -show:  -- the first word is "x" and is an argument to the redirection symbol,
/// -show:  -- and the second word is "abc" and is the command word.
/// -show:  line_state:getcommandwordindex() == 2
int32 line_state_lua::get_command_word_index(lua_State* state)
{
    uint32 index = m_line->get_command_word_index();
    if (m_shift)
    {
        const auto& words = m_line->get_words();
        const uint32 count = m_line->get_word_count();
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
int32 line_state_lua::get_word_count(lua_State* state)
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
/// <strong>Note:</strong>  The length refers to the substring in the line; it
/// omits leading and trailing quotes, but <em><strong>includes</strong></em>
/// embedded quotes.  <a href="#line_state:getword">line_state:getword()</a>
/// conveniently strips embedded quotes to help generators naturally complete
/// <code>"foo\"ba</code>
/// to <code>"foo\bar"</code>.
///
/// The table returned has the following scheme:
/// -show:  local t = line_state:getwordinfo(word_index)
/// -show:  -- t.offset     [integer] Offset where the word starts in the line_state:getline() string.
/// -show:  -- t.length     [integer] Length of the word (includes embedded quotes).
/// -show:  -- t.quoted     [boolean] Indicates whether the word is quoted.
/// -show:  -- t.delim      [string] The delimiter character, or an empty string.
/// -show:  -- t.cmd        [boolean | nil] true if the word is a built-in CMD command, otherwise nil.
/// -show:  -- t.alias      [boolean | nil] true if the word is a doskey alias, otherwise nil.
/// -show:  -- t.redir      [boolean | nil] true if the word is a redirection arg, otherwise nil.
int32 line_state_lua::get_word_info(lua_State* state)
{
    if (!lua_isnumber(state, LUA_SELF + 1))
        return 0;

    const words& words = m_line->get_words();
    uint32 index = int32(lua_tointeger(state, LUA_SELF + 1)) - 1 + m_shift;
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

    if (word.is_cmd_command)
    {
        lua_pushliteral(state, "cmd");
        lua_pushboolean(state, true);
        lua_rawset(state, -3);
    }

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
/// <strong>Note:</strong>  The returned word omits any quotes.  This helps
/// generators naturally complete <code>"foo\"ba</code> to
/// <code>"foo\bar"</code>.  The raw word including quotes can be obtained using
/// the <code>offset</code> and <code>length</code> fields from
/// <a href="#line_state:getwordinfo">line_state:getwordinfo()</a> to extract a
/// substring from the line returned by
/// <a href="#line_state:getline">line_state:getline()</a>.
///
/// <strong>However:</strong>  During
/// <code><a href="#the-getwordbreakinfo-function">generator:getwordbreakinfo()</a></code>
/// functions the returned word includes quotes, otherwise word break offsets
/// could be garbled.
int32 line_state_lua::get_word(lua_State* state)
{
    if (!lua_isnumber(state, LUA_SELF + 1))
        return 0;

    str<32> word;
    uint32 index = int32(lua_tointeger(state, LUA_SELF + 1)) - 1;
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
/// <strong>Note:</strong>  The returned word omits any quotes.  This helps
/// generators naturally complete <code>"foo\"ba</code> to
/// <code>"foo\bar"</code>.  The raw word including quotes can be obtained using
/// the <code>offset</code> and <code>length</code> fields from
/// <a href="#line_state:getwordinfo">line_state:getwordinfo()</a> to extract a
/// substring from the line returned by
/// <a href="#line_state:getline">line_state:getline()</a>.
/// -show:  line_state:getword(line_state:getwordcount()) == line_state:getendword()
///
/// <strong>However:</strong>  During
/// <code><a href="#the-getwordbreakinfo-function">generator:getwordbreakinfo()</a></code>
/// functions the returned word includes quotes, otherwise word break offsets
/// could be garbled.
int32 line_state_lua::get_end_word(lua_State* state)
{
    str<32> word;
    m_line->get_end_word(word);
    lua_pushlstring(state, word.c_str(), word.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getendwordoffset
/// -ver:   1.6.2
/// -ret:   integer
/// Returns the offset of the last word of the line. This is the word that
/// matches are being generated for.
int32 line_state_lua::get_end_word_offset(lua_State* state)
{
    assert(m_line->get_word_count() > 0);
    lua_pushinteger(state, m_line->get_end_word_offset() + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getrangeoffset
/// -ver:   1.6.1
/// -ret:   integer
/// Each line_state describes a range of text in a line.  This function returns
/// the offset to the start of the range described by this line_state.
///
/// For all commands after the first command in a line, the first space (if any)
/// is not part of the range.
/// See [line_state:getcommandoffset](#line_state:getcommandoffset) for details.
/// -show:  -- Given a line_state for the 2nd command in;  abc & ( @where   >nul  )  & xyz
/// -show:  line_state:getrangeoffset() == 9           --          ^
/// -show:  line_state:getcommandoffset() == 10        -            ^
/// -show:  line_state:getrangelength() == 15          --          <------------->
/// -show:
/// -show:  -- Given a line_state for the 2nd command in;  abc & (   @ where   >nul  )  & xyz
/// -show:  line_state:getrangeoffset() == 9           --          ^
/// -show:  line_state:getcommandoffset() == 13        --              ^
/// -show:  line_state:getrangelength() == 18          --          <---------------->
int32 line_state_lua::get_range_offset(lua_State* state)
{
    lua_pushinteger(state, m_line->get_range_offset() + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  line_state:getrangelength
/// -ver:   1.6.1
/// -ret:   integer
/// Each line_state describes a range of text in a line.  This function returns
/// the length of the range described by this line_state.
///
/// For all commands after the first command in a line, the first space (if any)
/// is not part of the range.
/// See [line_state:getcommandoffset](#line_state:getcommandoffset) for details.
/// -show:  -- Given a line_state for the 2nd command in;  abc & ( @where   >nul  )  & xyz
/// -show:  line_state:getrangeoffset() == 9           --          ^
/// -show:  line_state:getcommandoffset() == 10        --           ^
/// -show:  line_state:getrangelength() == 15          --          <------------->
/// -show:
/// -show:  -- Given a line_state for the 2nd command in;  abc & (   @ where   >nul  )  & xyz
/// -show:  line_state:getrangeoffset() == 9           --          ^
/// -show:  line_state:getcommandoffset() == 13        --              ^
/// -show:  line_state:getrangelength() == 18          --          <---------------->
int32 line_state_lua::get_range_length(lua_State* state)
{
    lua_pushinteger(state, m_line->get_range_length());
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::shift(lua_State* state)
{
    uint32 num = optinteger(state, LUA_SELF + 1, 0);

    if (num > 0)
    {
        num -= 1;
        if (num > m_line->get_word_count() || m_shift + num > m_line->get_word_count())
            num = m_line->get_word_count() - m_shift;
        if (!num)
            return 0;

        m_shift += num;
        m_tested_cmd_builtin = false;
    }

    lua_pushinteger(state, m_shift);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::reset_shift(lua_State* state)
{
    m_shift = 0;
    m_tested_cmd_builtin = false;
    return 0;
}

//------------------------------------------------------------------------------
static bool validate_unbreakchars(const char* s)
{
    for (; *s; ++s)
    {
        if (*s < 0x20 || *s > 0x7f)
            return false;
    }
    return true;
}

//------------------------------------------------------------------------------
inline bool is_unbreakchar(const char* unbreakchars, const char* line, uint32 len, uint32 index)
{
    if (index >= len)
        return false;
    const char c = line[index];
    if (!c)
        return false;
    const char* un = strchr(unbreakchars, c);
    if (!un)
        return false;
    if (index > 0 && line[index - 1] == ':')
        return false;
    return true;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::break_word(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    const auto length = checkinteger(state, LUA_SELF + 2);
    if (!_index.isnum() || !length.isnum())
        return 0;
    const uint32 index = _index - 1 + m_shift;

    const words& words = m_line->get_words();
    if (index >= words.size())
        return 0;

    const word& word = words[index];
    if (word.quoted)
        return 0;

    if (length <= 0 || uint32(length) >= word.length)
        return 0;

    line_state_copy* copy = make_line_state_copy(*m_line);
    copy->break_word(index, length);

    // PERF: Can it return itself if it's already a copy?  Does anything rely
    // on the copy operation, e.g. "original != line_state"?
    line_state_lua::make_new(state, copy, m_shift);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::unbreak_word(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    const char* unbreakchars = checkstring(state, LUA_SELF + 2);
    if (!_index.isnum() || !unbreakchars)
        return 0;
    if (!*unbreakchars) // No-op.
        return 0;
    if (!validate_unbreakchars(unbreakchars))
        return luaL_argerror(state, LUA_SELF + 2, "must contain only ASCII characters");
    const uint32 index = _index - 1 + m_shift;

    const words& words = m_line->get_words();
    if (index >= words.size())
        return 0;

    const word& word = words[index];
    if (word.quoted)
        return 0;

    // Cannot unbreak further than the beginning of the next word!
    uint32 max_len = m_line->get_words_limit();
    if (index + 1 < words.size())
        max_len = min(max_len, words[index + 1].offset);

    const char* const line = m_line->get_line();
    const uint32 comma_index = word.offset + word.length;
    if (!is_unbreakchar(unbreakchars, line, max_len, comma_index))
        return 0;

    uint32 append_len = 1;
    while (is_unbreakchar(unbreakchars, line, max_len, comma_index + append_len))
        ++append_len;

    line_state_copy* copy = make_line_state_copy(*m_line);

    const uint32 new_len = word.length + append_len;
    const bool into_next = copy->unbreak_word(index, new_len);

    // PERF: Can it return itself if it's already a copy?  Does anything rely
    // on the copy operation, e.g. "original != line_state"?
    line_state_lua::make_new(state, copy, m_shift);
    lua_pushboolean(state, into_next);
    lua_pushinteger(state, new_len);
    return 3;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::set_alias(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    const bool value = lua_isnoneornil(state, LUA_SELF + 2) || lua_toboolean(state, LUA_SELF + 2);
    if (!_index.isnum())
        return 0;
    const uint32 index = _index - 1 + m_shift;

    const words& words = m_line->get_words();
    if (index >= words.size())
        return 0;

    line_state_copy* copy = make_line_state_copy(*m_line);
    copy->set_alias(index, value);

    // PERF: Can it return itself if it's already a copy?  Does anything rely
    // on the copy operation, e.g. "original != line_state"?
    line_state_lua::make_new(state, copy, m_shift);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::overwrite_from(lua_State* state)
{
    line_state_lua* from = check(state, LUA_SELF + 1);
    if (!from)
        return 0;

    const bool ok = const_cast<line_state*>(m_line)->overwrite_from(from->m_line);
    assert(ok);
    lua_pushboolean(state, ok);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 line_state_lua::test_cmd_builtin(lua_State* state)
{
    if (m_tested_cmd_builtin)
        return 0;
    m_tested_cmd_builtin = true;

    const uint32 index = m_line->get_command_word_index() + m_shift;
    const words& words = m_line->get_words();
    if (index >= words.size())
        return 0;

    const word& word = words[index];
    if (word.quoted)
        return 0;

    // Is it a builtin cmd command?
    str<> tmp;
    tmp.concat(m_line->get_line() + word.offset, word.length);
    if (!is_cmd_command(tmp.c_str()))
    {
        assert(!word.is_cmd_command);
        return 0;
    }

    // Definitely need to make a copy:  either the word needs to be marked as
    // a builtin cmd command, or the word needs to be joined with the next
    // adjacent word.
    line_state_copy* copy = make_line_state_copy(*m_line);
    copy->test_cmd_builtin(index);

    // PERF: Can it return itself if it's already a copy?  Does anything rely
    // on the copy operation, e.g. "original != line_state"?
    line_state_lua::make_new(state, copy, m_shift);
    return 1;
}
