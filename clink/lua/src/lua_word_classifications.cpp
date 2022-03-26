// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_word_classifications.h"
#include "lua_state.h"
#include "line_state_lua.h"

#include <lib/line_state.h>
#include <lib/word_classifications.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <assert.h>

//------------------------------------------------------------------------------
const char* const lua_word_classifications::c_name = "lua_word_classifications";
const lua_word_classifications::method lua_word_classifications::c_methods[] = {
    { "classifyword",     &classify_word },
    { "applycolor",       &apply_color },
    // UNDOCUMENTED; internal use only.
    { "shift",            &shift },
    {}
};



//------------------------------------------------------------------------------
lua_word_classifications::lua_word_classifications(word_classifications& classifications, unsigned int index_offset, unsigned int command_word_index, unsigned int num_words)
: m_classifications(classifications)
, m_index_offset(index_offset)
, m_command_word_index(command_word_index)
, m_num_words(num_words)
{
}

//------------------------------------------------------------------------------
/// -name:  word_classifications:classifyword
/// -ver:   1.1.18
/// -arg:   word_index:integer
/// -arg:   word_class:string
/// -arg:   [overwrite:boolean]
/// This classifies the indicated word so that it can be colored appropriately.
///
/// The <span class="arg">word_class</span> is one of the following codes:
///
/// <table>
/// <tr><th>Code</th><th>Classification</th><th>Clink Color Setting</th></tr>
/// <tr><td><code>"a"</code></td><td>Argument; used for words that match a list of preset argument matches.</td><td><code>color.arg</code> or <code>color.input</code></td></tr>
/// <tr><td><code>"c"</code></td><td>Shell command; used for CMD command names.</td><td><code>color.cmd</code></td></tr>
/// <tr><td><code>"d"</code></td><td>Doskey alias.</td><td><code>color.doskey</code></td></tr>
/// <tr><td><code>"f"</code></td><td>Flag; used for flags that match a list of preset flag matches.</td><td><code>color.flag</code></td></tr>
/// <tr><td><code>"x"</code></td><td>Executable; used for the first word when it is not a command or doskey alias, but is an executable name that exists.</td><td><code>color.executable</code></td></tr>
/// <tr><td><code>"u"</code></td><td>Unrecognized; used for the first word when it is not a command, doskey alias, or recognized executable name.</td><td><code>color.unrecognized</code></td></tr>
/// <tr><td><code>"o"</code></td><td>Other; used for file names and words that don't fit any of the other classifications.</td><td><code>color.input</code></td></tr>
/// <tr><td><code>"n"</code></td><td>None; used for words that aren't recognized as part of the expected input syntax.</td><td><code>color.unexpected</code></td></tr>
/// <tr><td><code>"m"</code></td><td>Prefix that can be combined with another code (for the first word) to indicate the command has an argmatcher (e.g. <code>"mc"</code> or <code>"md"</code>).</td><td><code>color.argmatcher</code> or the other code's color</td></tr>
/// </table>
///
/// By default the classification is applied to the word even if the word has
/// already been classified.  But if <span class="arg">overwrite</span> is
/// <code>false</code> the word is only classified if it hasn't been yet.
///
/// See <a href="#classifywords">Coloring the Input Text</a> for more
/// information.
int lua_word_classifications::classify_word(lua_State* state)
{
    if (!lua_isnumber(state, 1) || !lua_isstring(state, 2))
        return 0;

    const unsigned int index = static_cast<unsigned int>(int(lua_tointeger(state, 1)) - 1) + m_shift;
    const char* s = lua_tostring(state, 2);
    bool overwrite = !lua_isboolean(state, 3) || lua_toboolean(state, 3);
    if (!s)
        return 0;
    if (index >= m_num_words)
        return luaL_error(state, "word_index out of bounds");

    const bool has_argmatcher = (*s == 'm');
    if (has_argmatcher)
        s++;

    char wc;
    switch (*s)
    {
    case 'o':
    case 'u':
    case 'x':
    case 'c':
    case 'd':
    case 'a':
    case 'f':
    case 'n':
        wc = *s;
        break;
    default:
        wc = 'o';
        break;
    }

    m_classifications.classify_word(m_index_offset + index, wc, overwrite);
    if (has_argmatcher && index == m_command_word_index)
        m_classifications.set_word_has_argmatcher(m_index_offset + index);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  word_classifications:applycolor
/// -ver:   1.1.49
/// -arg:   start:integer
/// -arg:   length:integer
/// -arg:   color:string
/// -arg:   [overwrite:boolean]
/// Applies an ANSI <a href="https://en.wikipedia.org/wiki/ANSI_escape_code#SGR">SGR escape code</a>
/// to some characters in the input line.
///
/// <span class="arg">start</span> is where to begin applying the SGR code.
///
/// <span class="arg">length</span> is the number of characters to affect.
///
/// <span class="arg">color</span> is the SGR parameters sequence to apply (for example <code>"7"</code> is the code for reverse video, which swaps the foreground and background colors).
///
/// By default the color is applied to the characters even if some of them are
/// already colored.  But if <span class="arg">overwrite</span> is
/// <code>false</code> each character is only colored if it hasn't been yet.
///
/// See <a href="#classifywords">Coloring the Input Text</a> for more
/// information.
///
/// Note: an input line can have up to 100 different unique color strings
/// applied, and then this stops applying new colors.  The counter gets reset
/// when the onbeginedit event is sent.
int lua_word_classifications::apply_color(lua_State* state)
{
    if (!lua_isnumber(state, 1) || !lua_isnumber(state, 2) || !lua_isstring(state, 3))
        return 0;

    unsigned int start = (unsigned int)(lua_tointeger(state, 1)) - 1;
    unsigned int length = (unsigned int)(lua_tointeger(state, 2));
    const char* color = lua_tostring(state, 3);
    bool overwrite = !lua_isboolean(state, 4) || lua_toboolean(state, 4);
    if (!color)
        return 0;

    char face = m_classifications.ensure_face(color);
    if (!face)
        return 0;

    m_classifications.apply_face(start, length, face, overwrite);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int lua_word_classifications::shift(lua_State* state)
{
    unsigned int num = optinteger(state, 1, 0);
    unsigned int cmd = optinteger(state, 2, 1);

    if (num > 0)
    {
        num -= 1;
        if (num > m_num_words || m_shift + num > m_num_words)
            num = m_num_words - m_shift;
        if (!num)
            return 0;

        m_shift += num;
        if (cmd > m_num_words || m_shift + cmd - 1 > m_num_words)
            cmd = 1;
        m_command_word_index = m_shift + cmd - 1;
    }

    lua_pushinteger(state, m_shift);
    lua_pushinteger(state, m_command_word_index);
    return 2;
}
