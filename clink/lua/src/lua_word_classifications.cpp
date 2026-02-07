// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_word_classifier.h"
#include "lua_word_classifications.h"
#include "lua_state.h"
#include "line_state_lua.h"

#include <lib/line_state.h>
#include <lib/word_classifications.h>
#include <lib/display_readline.h>

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
    { "_set_line_state",  &set_line_state },
    {}
};



//------------------------------------------------------------------------------
lua_word_classifications::lua_word_classifications(word_classifications& classifications, uint32 index_command)
: m_classifications(classifications)
, m_index_command(index_command)
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
/// <tr><td><code>"a"</code></td><td>Argument; used for words that match a list of preset argument matches.</td><td><code><a href="#color_arg">color.arg</a></code> or <code><a href="#color_input">color.input</a></code></td></tr>
/// <tr><td><code>"c"</code></td><td>Shell command; used for CMD command names.</td><td><code><a href="#color_cmd">color.cmd</a></code></td></tr>
/// <tr><td><code>"d"</code></td><td>Doskey alias.</td><td><code><a href="#color_doskey">color.doskey</a></code></td></tr>
/// <tr><td><code>"f"</code></td><td>Flag; used for flags that match a list of preset flag matches.</td><td><code><a href="#color_flag">color.flag</a></code></td></tr>
/// <tr><td><code>"x"</code></td><td>Executable; used for the first word when it is not a command or doskey alias, but is an executable name that exists.</td><td><code><a href="#color_executable">color.executable</a></code></td></tr>
/// <tr><td><code>"u"</code></td><td>Unrecognized; used for the first word when it is not a command, doskey alias, or recognized executable name.</td><td><code><a href="#color_unrecognized">color.unrecognized</a></code></td></tr>
/// <tr><td><code>"o"</code></td><td>Other; used for file names and words that don't fit any of the other classifications.</td><td><code><a href="#color_input">color.input</a></code></td></tr>
/// <tr><td><code>"n"</code></td><td>None; used for words that aren't recognized as part of the expected input syntax.</td><td><code><a href="#color_unexpected">color.unexpected</a></code></td></tr>
/// <tr><td><code>"m"</code></td><td>Prefix that can be combined with another code (for the first word) to indicate the command has an argmatcher (e.g. <code>"mc"</code> or <code>"md"</code>).</td><td><code><a href="#color_argmatcher">color.argmatcher</a></code> or the other code's color</td></tr>
/// </table>
///
/// By default the classification is applied to the word even if the word has
/// already been classified.  But if <span class="arg">overwrite</span> is
/// <code>false</code> the word is only classified if it hasn't been yet.
///
/// See <a href="#classifywords">Coloring the Input Text</a> for more
/// information.
int32 lua_word_classifications::classify_word(lua_State* state)
{
    const auto _index = checkinteger(state, LUA_SELF + 1);
    const char* s = checkstring(state, LUA_SELF + 2);
    bool overwrite = !lua_isboolean(state, LUA_SELF + 3) || lua_toboolean(state, LUA_SELF + 3);
    if (!_index.isnum() || !s)
        return 0;
    const uint32 direct_index = _index - 1;
    if (direct_index >= m_words.size())
        return luaL_argerror(state, LUA_SELF + 1, "word index out of bounds");
    const uint32 shifted_index = direct_index + m_shift;

    const bool has_m = (*s == 'm');
    const bool has_argmatcher = (has_m && direct_index == m_command_word_index);
    if (has_m)
        s++;

    char face;
    switch (*s)
    {
    case 'o':   face = FACE_OTHER; break;
    case 'u':   face = FACE_UNRECOGNIZED; break;
    case 'x':   face = FACE_EXECUTABLE; break;
    case 'c':   face = FACE_COMMAND; break;
    case 'd':   face = FACE_ALIAS; break;
    case 'a':   face = FACE_ARGUMENT; break;
    case 'f':   face = FACE_FLAG; break;
    case 'n':   face = FACE_NONE; break;
    default:    face = FACE_OTHER; break;
    }

    if (has_argmatcher && m_classifications.can_show_argmatchers())
        face = FACE_ARGMATCHER;

    const auto& word = m_words[direct_index];
    m_classifications.apply_face(true, word.offset, word.length, face, overwrite);

    extern bool is_test_harness();
    if (m_test || is_test_harness())
        m_classifications.classify_word(m_index_command, shifted_index, *s, has_argmatcher, overwrite);

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
/// <strong>Note:</strong> An input line can have up to 100 different unique
/// color strings applied, and then this stops applying new colors.  The
/// counter gets reset when the onbeginedit event is sent.
int32 lua_word_classifications::apply_color(lua_State* state)
{
    auto start = checkinteger(state, LUA_SELF + 1);
    const auto length = checkinteger(state, LUA_SELF + 2);
    const char* color = checkstring(state, LUA_SELF + 3);
    bool overwrite = !lua_isboolean(state, LUA_SELF + 4) || lua_toboolean(state, LUA_SELF + 4);
    if (!start.isnum() || !length.isnum() || !color)
        return 0;
    start.minus_one();

    char face = m_classifications.ensure_face(color);
    if (!face)
        return 0;

    m_classifications.apply_face(false, start, length, face, overwrite);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 lua_word_classifications::set_line_state(lua_State* state)
{
    line_state_lua* lsl = line_state_lua::check(state, LUA_SELF + 1);
    const bool test = lua_toboolean(state, LUA_SELF + 2);
    if (!lsl)
        return 0;

    const line_state* line_state = lsl->get_line_state();
    m_words.clear();
    m_command_word_index = line_state->get_command_word_index();
    m_shift = lsl->get_shift();
    m_test |= test;     // Latch; arguments.lua doesn't know when to pass it.

    const auto& words = line_state->get_words();
    for (uint32 i = m_shift; i < words.size(); ++i)
    {
        word_def w;
        w.offset = words[i].offset;
        w.length = words[i].length;
        m_words.emplace_back(w);
    }
    return 0;
}
