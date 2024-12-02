// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "terminal/scroll.h"
#include "terminal/screen_buffer.h" // for set_console_title
#include "terminal/printer.h"
#include "terminal/find_line.h"
#include "terminal/ecma48_iter.h"
#include "terminal/wcwidth.h"
#include "terminal/terminal.h"
#include "terminal/terminal_in.h"
#include "terminal/terminal_helpers.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <lib/ellipsify.h>

extern "C" {
#include <readline/readline.h>
extern "C" int _rl_last_v_pos;
};

//------------------------------------------------------------------------------
static SHORT GetConsoleNumLines(const CONSOLE_SCREEN_BUFFER_INFO& csbi)
{
    // Calculate the bottom as the line immediately preceding the beginning of
    // the Readline input line.  That may contain some of the prompt text, if
    // the prompt text is more than one line.
    SHORT bottom_Y = csbi.dwCursorPosition.Y - _rl_last_v_pos;
    return bottom_Y + 1;
}

//------------------------------------------------------------------------------
class input_scope
{
public:
    input_scope(terminal_in* in)
    : m_in(in)
    , m_old_tester(in->set_key_tester(nullptr))
    {
        // No key tester; accept all key sequences regardless whether they're
        // bound in Readline.  This is important because this shouldn't limit
        // input, and also because in a luafunc macro the rl_module key tester
        // implementation causes heap corruption.
        m_in->begin(false);
    }

    ~input_scope()
    {
        m_in->end(false);
        m_in->set_key_tester(m_old_tester);
    }

private:
    terminal_in* const  m_in;
    key_tester* const   m_old_tester;
};



//------------------------------------------------------------------------------
/// -name:  console.scroll
/// -ver:   1.1.20
/// -arg:   mode:string
/// -arg:   amount:integer
/// -ret:   integer
/// Scrolls the console screen buffer and returns the number of lines scrolled
/// up (negative) or down (positive).
///
/// The <span class="arg">mode</span> specifies how to scroll:
/// <table>
/// <tr><th>Mode</th><th>Description</th></tr>
/// <tr><td>"line"</td><td>Scrolls by <span class="arg">amount</span> lines;
/// negative is up and positive is down.</td></tr>
/// <tr><td>"page"</td><td>Scrolls by <span class="arg">amount</span> pages;
/// negative is up and positive is down.</td></tr>
/// <tr><td>"end"</td><td>Scrolls to the top if <span class="arg">amount</span>
/// is negative, or to the bottom if positive.</td></tr>
/// <tr><td>"absolute"</td><td>Scrolls to line <span class="arg">amount</span>,
/// from 1 to <a href="#console.getnumlines">console.getnumlines()</a>.</td></tr>
/// </table>
static int32 scroll(lua_State* state)
{
    const char* mode = checkstring(state, 1);
    auto amount = checkinteger(state, 2);
    if (!mode || !amount.isnum())
        return 0;

    SCRMODE scrmode = SCR_BYLINE;
    if (stricmp(mode, "page") == 0)
        scrmode = SCR_BYPAGE;
    else if (stricmp(mode, "end") == 0)
        scrmode = SCR_TOEND;
    else if (stricmp(mode, "absolute") == 0)
    {
        scrmode = SCR_ABSOLUTE;
        amount.minus_one();
    }

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    int32 scrolled = ScrollConsoleRelative(h, amount, scrmode);
    lua_pushinteger(state, scrolled);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.cellcount
/// -ver:   1.2.5
/// -arg:   text:string
/// -ret:   integer
/// Returns the count of visible character cells that would be consumed if the
/// <span class="arg">text</span> string were output to the console, accounting
/// for any ANSI escape codes that may be present in the text.
///
/// <strong>Note:</strong> Backspace characters and line endings are counted
/// as visible character cells and will skew the resulting count.
static int32 get_cell_count(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    uint32 cells = 0;
    ecma48_processor(in, nullptr/*out*/, &cells);
    lua_pushinteger(state, cells);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.plaintext
/// -ver:   1.2.5
/// -arg:   text:string
/// -ret:   string, integer
/// Returns the input <span class="arg">text</span> with ANSI escape codes
/// removed, and the count of visible character cells that would be consumed
/// if the text were output to the console.
///
/// <strong>Note:</strong> Backspace characters and line endings are counted
/// as visible character cells and will skew the resulting count.
static int32 get_plain_text(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    str<> out;
    uint32 cells = 0;
    ecma48_processor(in, &out, &cells, ecma48_processor_flags::plaintext);
    lua_pushlstring(state, out.c_str(), out.length());
    lua_pushinteger(state, cells);
    return 2;
}

//------------------------------------------------------------------------------
/// -name:  console.explodeansi
/// -ver:   1.6.1
/// -arg:   text:string
/// -ret:   table
/// Splits <span class="arg">text</span> on ANSI escape code boundaries and
/// returns a table containing the substrings.
/// -show:  console.explodeansi("\x1b[7mReverse\x1b[0;1mBold\x1b[m\x1b[K")
/// -show:  -- returns the following table:
/// -show:  --  {
/// -show:  --      "\x1b[7m",
/// -show:  --      "Reverse",
/// -show:  --      "\x1b[0;1m",
/// -show:  --      "Bold",
/// -show:  --      "\x1b[m",
/// -show:  --      "\x1b[K",
/// -show:  --  }
int32 explode_ansi(lua_State* state)
{
    const char* text = checkstring(state, 1);
    if (!text)
        return 0;

    int32 count = 0;
    lua_createtable(state, 16, 0);

    ecma48_state _state;
    ecma48_iter iter(text, _state);
    while (const ecma48_code &code = iter.next())
    {
        lua_pushlstring(state, code.get_pointer(), code.get_length());
        lua_rawseti(state, -2, ++count);
    }

    if (!count)
    {
        lua_pushlstring(state, "", 0);
        lua_rawseti(state, -2, ++count);
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getwidth
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the width of the console screen buffer in characters.
static int32 get_width(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.dwSize.X);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getheight
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the number of visible lines of the console screen buffer.
static int32 get_height(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.srWindow.Bottom + 1 - csbiInfo.srWindow.Top);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getnumlines
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the total number of lines in the console screen buffer.
static int32 get_num_lines(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, GetConsoleNumLines(csbiInfo));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.gettop
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the current top line (scroll position) in the console screen buffer.
static int32 get_top(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.srWindow.Top + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getcursorpos
/// -ver:   1.4.28
/// -ret:   integer, integer
/// Returns the current cursor column and row in the console screen buffer.
/// The row is between 1 and <a href="#console.getnumlines">console.getnumlines()</a>.
/// The column is between 1 and <a href="#console.getwidth">console.getwidth()</a>.
/// -show:  local x, y = console.getcursorpos()
static int32 get_cursor_pos(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.dwCursorPosition.X + 1);
    lua_pushinteger(state, csbiInfo.dwCursorPosition.Y + 1);
    return 2;
}

//------------------------------------------------------------------------------
/// -name:  console.getlinetext
/// -ver:   1.1.20
/// -arg:   line:integer
/// -ret:   string
/// Returns the text from line number <span class="arg">line</span>, from 1 to
/// <a href="#console.getnumlines">console.getnumlines()</a>.
///
/// Any trailing whitespace is stripped before returning the text.
static int32 get_line_text(lua_State* state)
{
    if (!g_printer)
        return 0;

    const auto _line = checkinteger(state, 1);
    if (!_line.isnum())
        return 0;
    SHORT line = _line - 1;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    line = min<int32>(line, GetConsoleNumLines(csbi) - 1);
    line = max<int32>(line, 0);

    str_moveable out;
    if (!g_printer->get_line_text(line, out))
        return 0;

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.gettitle
/// -ver:   1.1.32
/// -ret:   string
/// Returns the console title text.
static int32 get_title(lua_State* state)
{
    wstr<16> title;
    title.reserve(4096);

    DWORD len = GetConsoleTitleW(title.data(), title.size());
    if (len || GetLastError() == ERROR_SUCCESS)
    {
        str<> out;
        to_utf8(out, title.c_str());

        lua_pushstring(state, out.c_str());
        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  console.settitle
/// -ver:   1.1.32
/// -arg:   title:string
/// Sets the console title text.
static int32 set_title(lua_State* state)
{
    const char* title = checkstring(state, 1);
    if (!title)
        return 0;

    set_console_title(title);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  console.islinedefaultcolor
/// -ver:   1.1.20
/// -arg:   line:integer
/// -ret:   boolean
/// Returns whether line number <span class="arg">line</span> uses only the
/// default text color.
static int32 is_line_default_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    const auto _line = checkinteger(state, 1);
    if (!_line.isnum())
        return 0;
    SHORT line = _line - 1;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    line = min<int32>(line, GetConsoleNumLines(csbi) - 1);
    line = max<int32>(line, 0);

    int32 result = g_printer->is_line_default_color(line);

    if (result < 0)
        return 0;

    lua_pushboolean(state, result > 0);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.linehascolor
/// -ver:   1.1.21
/// -arg:   line:integer
/// -arg:   [attr:integer]
/// -arg:   [attrs:table of integers]
/// -arg:   [mask:string]
/// -ret:   boolean
/// Returns whether line number <span class="arg">line</span> contains the DOS
/// color code <span class="arg">attr</span>, or any of the DOS color codes in
/// <span class="arg">attrs</span> (either an integer or a table of integers
/// must be provided, but not both).  <span class="arg">mask</span> is optional
/// and can be "fore" or "back" to only match foreground or background colors,
/// respectively.
///
/// The low 4 bits of the color code are the foreground color, and the high 4
/// bits of the color code are the background color.  This refers to the default
/// 16 color palette used by console windows.  When 256 color or 24-bit color
/// ANSI escape codes have been used, the closest of the 16 colors is used.
///
/// To build a color code, add the corresponding Foreground color and the
/// Background color values from this table:
///
/// <table><tr><th align="center">Foreground</th><th align="center">Background</th><th>Color</th></tr>
/// <tr><td align="center">0</td><td align="center">0</td><td><div class="colorsample" style="background-color:#000000">&nbsp;</div> Black</td></tr>
/// <tr><td align="center">1</td><td align="center">16</td><td><div class="colorsample" style="background-color:#000080">&nbsp;</div> Dark Blue</td></tr>
/// <tr><td align="center">2</td><td align="center">32</td><td><div class="colorsample" style="background-color:#008000">&nbsp;</div> Dark Green</td></tr>
/// <tr><td align="center">3</td><td align="center">48</td><td><div class="colorsample" style="background-color:#008080">&nbsp;</div> Dark Cyan</td></tr>
/// <tr><td align="center">4</td><td align="center">64</td><td><div class="colorsample" style="background-color:#800000">&nbsp;</div> Dark Red</td></tr>
/// <tr><td align="center">5</td><td align="center">80</td><td><div class="colorsample" style="background-color:#800080">&nbsp;</div> Dark Magenta</td></tr>
/// <tr><td align="center">6</td><td align="center">96</td><td><div class="colorsample" style="background-color:#808000">&nbsp;</div> Dark Yellow</td></tr>
/// <tr><td align="center">7</td><td align="center">112</td><td><div class="colorsample" style="background-color:#c0c0c0">&nbsp;</div> Gray</td></tr>
/// <tr><td align="center">8</td><td align="center">128</td><td><div class="colorsample" style="background-color:#808080">&nbsp;</div> Dark Gray</td></tr>
/// <tr><td align="center">9</td><td align="center">144</td><td><div class="colorsample" style="background-color:#0000ff">&nbsp;</div> Bright Blue</td></tr>
/// <tr><td align="center">10</td><td align="center">160</td><td><div class="colorsample" style="background-color:#00ff00">&nbsp;</div> Bright Green</td></tr>
/// <tr><td align="center">11</td><td align="center">176</td><td><div class="colorsample" style="background-color:#00ffff">&nbsp;</div> Bright Cyan</td></tr>
/// <tr><td align="center">12</td><td align="center">192</td><td><div class="colorsample" style="background-color:#ff0000">&nbsp;</div> Bright Red</td></tr>
/// <tr><td align="center">13</td><td align="center">208</td><td><div class="colorsample" style="background-color:#ff00ff">&nbsp;</div> Bright Magenta</td></tr>
/// <tr><td align="center">14</td><td align="center">224</td><td><div class="colorsample" style="background-color:#ffff00">&nbsp;</div> Bright Yellow</td></tr>
/// <tr><td align="center">15</td><td align="center">240</td><td><div class="colorsample" style="background-color:#ffffff">&nbsp;</div> White</td></tr>
/// </table>
static int32 line_has_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    const auto _line = checkinteger(state, 1);
    bool has_attrs = lua_isnumber(state, 2) || lua_istable(state, 2);
    if (!has_attrs)
        luaL_argerror(state, 2, "must be number or table of numbers");
    const char* mask_name = optstring(state, 3, "");
    if (!_line.isnum() || !has_attrs || !mask_name)
        return 0;
    SHORT line = _line - 1;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    line = min<int32>(line, GetConsoleNumLines(csbi) - 1);
    line = max<int32>(line, 0);

    BYTE mask = 0xff;
    if (mask_name && *mask_name)
    {
        if (strcmp(mask_name, "fore") == 0)         mask = 0x0f;
        else if (strcmp(mask_name, "back") == 0)    mask = 0xf0;
        else if (strcmp(mask_name, "both") == 0)    mask = 0xff;
    }

    int32 result;

    if (lua_isnumber(state, 2))
    {
        BYTE attr = BYTE(lua_tointeger(state, 2));
        result = g_printer->line_has_color(line, &attr, 1, mask);
    }
    else
    {
        BYTE attrs[32];
        int32 num_attrs = 0;

        for (int32 i = 1; num_attrs <= sizeof_array(attrs); i++)
        {
            lua_rawgeti(state, 2, i);
            if (lua_isnil(state, -1))
            {
                lua_pop(state, 1);
                break;
            }

            attrs[num_attrs++] = BYTE(lua_tointeger(state, -1));

            lua_pop(state, 1);
        }
        result = g_printer->line_has_color(line, attrs, num_attrs, mask);
    }

    if (result < 0)
        return 0;

    lua_pushboolean(state, result > 0);
    return 1;
}

static int32 find_line(lua_State* state, int32 direction)
{
    if (!g_printer)
        return 0;

    int32 arg = 1;

    // Starting line number is required.
    const auto _starting_line = checkinteger(state, arg);
    if (!_starting_line.isnum())
        return 0;
    SHORT starting_line = _starting_line - 1;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT num_lines = GetConsoleNumLines(csbi);

    starting_line = min<int32>(starting_line, num_lines - 1);
    starting_line = max<int32>(starting_line, 0);
    arg++;

    int32 distance;
    if (direction > 0)
        distance = num_lines - starting_line + 1;
    else
        distance = 0 - starting_line - 1;

    // Text? Mode?
    const char* text = nullptr;
    find_line_mode mode = find_line_mode::none;
    if (lua_isstring(state, arg))
    {
        text = lua_tostring(state, arg);
        arg++;

        if (lua_isstring(state, arg))
        {
            const char* mode_string = lua_tostring(state, arg);
            arg++;

            str<32> token;
            str_tokeniser modes(mode_string, " ,;");
            while (modes.next(token))
            {
                if (token.equals("regex"))
                    mode |= find_line_mode::use_regex;
                else if (token.equals("icase"))
                    mode |= find_line_mode::ignore_case;
            }
        }
    }

    // Attr? Attrs?
    BYTE attrs[32];
    int32 num_attrs = 0;
    if (lua_isnumber(state, arg))
    {
        attrs[0] = BYTE(lua_tointeger(state, arg));
        num_attrs = 1;
        arg++;
    }
    else if (lua_istable(state, arg))
    {
        for (int32 i = 1; num_attrs <= sizeof_array(attrs); i++)
        {
            lua_rawgeti(state, arg, i);
            if (lua_isnil(state, -1))
            {
                lua_pop(state, 1);
                break;
            }

            attrs[num_attrs++] = BYTE(lua_tointeger(state, -1));

            lua_pop(state, 1);
        }
        arg++;
    }

    BYTE mask = 0xff;
    if (num_attrs && lua_isstring(state, arg))
    {
        const char* mask_name = lua_tostring(state, arg);
        arg++;

        if (mask_name && *mask_name)
        {
            if (strcmp(mask_name, "fore") == 0)         mask = 0x0f;
            else if (strcmp(mask_name, "back") == 0)    mask = 0xf0;
            else if (strcmp(mask_name, "both") == 0)    mask = 0xff;
        }
    }

    int32 line_found = g_printer->find_line(starting_line, distance, text, mode, attrs, num_attrs, mask);

    lua_pushinteger(state, line_found + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.findprevline
/// -ver:   1.1.21
/// -arg:   starting_line:integer
/// -arg:   [text:string]
/// -arg:   [mode:string]
/// -arg:   [attr:integer | table of integers]
/// -arg:   [mask:string]
/// -ret:   integer
/// Searches upwards (backwards) for a line containing the specified text and/or
/// attributes, starting at line <span class="arg">starting_line</span>.  The
/// matching line number is returned, or 0 if no matching line is found, or -1
/// if an invalid regular expression is provided.
///
/// You can search for text, attributes, or both.  Include the
/// <span class="arg">text</span> argument to search for text, and include
/// either the <span class="arg">attr</span> or <span class="arg">attrs</span>
/// argument to search for attributes.  If both text and attribute(s) are
/// passed, then the attribute(s) must be found within the found text.  If only
/// attribute(s) are passed, then they must be found anywhere in the line.  See
/// <a href="#console.linehascolor">console.linehascolor()</a> for more
/// information about the color codes.
///
/// The <span class="arg">mode</span> argument selects how the search behaves.
/// To use a regular expression, pass "regex".  To use a case insensitive
/// search, pass "icase".  These can be combined by separating them with a
/// comma.  The regular expression syntax is the ECMAScript syntax described
/// <a href="https://docs.microsoft.com/en-us/cpp/standard-library/regular-expressions-cpp">here</a>.
///
/// Any trailing whitespace is ignored when searching.  This especially affects
/// the <code>$</code> (end of line) regex operator.
///
/// <span class="arg">mask</span> is optional and can be "fore" or "back" to
/// only match foreground or background colors, respectively.
///
/// <strong>Note:</strong> Although most of the arguments are optional, the
/// order of provided arguments is important.
///
/// For more information, see <a href="#findlineexample">this example</a> of
/// using this in some <a href="#luakeybindings">luafunc: macros</a>.
static int32 find_prev_line(lua_State* state)
{
    return find_line(state, -1);
}

//------------------------------------------------------------------------------
/// -name:  console.findnextline
/// -ver:   1.1.21
/// -arg:   starting_line:integer
/// -arg:   [text:string]
/// -arg:   [mode:string]
/// -arg:   [attr:integer | table of integers]
/// -arg:   [mask:string]
/// -ret:   integer
/// Searches downwards (forwards) for a line containing the specified text
/// and/or attributes, starting at line <span class="arg">starting_line</span>.
/// The matching line number is returned, or 0 if no matching line is found.
///
/// This behaves the same as
/// <a href="#console.findprevline">console.findprevline()</a> except that it
/// searches in the opposite direction.
static int32 find_next_line(lua_State* state)
{
    return find_line(state, +1);
}

//------------------------------------------------------------------------------
/// -name:  console.readinput
/// -ver:   1.2.29
/// -arg:   no_cursor:boolean
/// -ret:   string | nil
/// Reads one key sequence from the console input.  If no input is available, it
/// waits until input becomes available.
///
/// This returns the full key sequence string for the pressed key.
/// For example, <kbd>A</kbd> is <code>"A"</code> and <kbd>Home</kbd> is
/// <code>"\027[A"</code>, etc.  Nil is returned when an interrupt occurs by
/// pressing <kbd>Ctrl</kbd>-<kbd>Break</kbd>.
///
/// See <a href="#discoverkeysequences">Discovering Key Sequences</a> for
/// information on how to find the key sequence for a key.
///
/// In Clink v1.3.42 and higher, passing true for
/// <span class="arg">no_cursor</span> avoids modifying the cursor visibility or
/// position.
///
/// <strong>Note:</strong> Mouse input is not supported.
static int32 read_input(lua_State* state)
{
    const bool no_cursor = (lua_isboolean(state, 1) && lua_toboolean(state, 1));

    terminal_in* const in = get_lua_terminal_input();
    if (!in)
        return 0;

    str<> key;

    input_scope is(in);
    const bool restore_cursor = (!no_cursor && show_cursor(false));

    // Get one full input key sequence.
    bool select = !in->available(0);
    while (true)
    {
        int32 k = in->read();
        if (k == terminal_in::input_none)
        {
            if (!select)
                break;
            in->select();
            select = false;
            continue;
        }

        if (k == terminal_in::input_abort)
            break;

        if (k == terminal_in::input_terminal_resize ||
            k == terminal_in::input_exit)
            continue;

        if (k < 0)
        {
            assert(k >= 0);
            continue;
        }

        char c = static_cast<char>(k);
        key.concat_no_truncate(&c, 1);
    }

    if (restore_cursor)
        show_cursor(true);

    if (key.empty())
        return 0;

    // Nul bytes are lost in Lua.  But only Ctrl+@ should produce a string with
    // a nul byte.  And since Ctrl+@ is "\0", an empty string means Ctrl+@ was
    // the input.
    lua_pushlstring(state, key.c_str(), key.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.checkinput
/// -ver:   1.3.42
/// -arg:   [timeout:number]
/// -ret:   boolean
/// Checks whether input is available.
///
/// The optional <span class="arg">timeout</span> is the number of seconds to
/// wait for input to be available (use a floating point number for fractional
/// seconds).  The default is 0 seconds, which returns immediately if input is
/// not available.
///
/// If input is available before the <span class="arg">timeout</span> is
/// reached, the return value is true.  Use
/// <a href="#console.readinput">console.readinput()</a> to read the available
/// input.
///
/// In Clink v1.6.0 and higher, when <span class="arg">timeout</span> is
/// negative, the timeout is infinite.
///
/// <strong>Note:</strong> Mouse input is not supported.
/// -show:  if console.checkinput() then
/// -show:  &nbsp;   local key = console.readinput() -- Returns immediately since input is available.
/// -show:  &nbsp;   if key == "\x03" or key == "\x1b[27;27~" or key == "\x1b" then
/// -show:  &nbsp;       -- Ctrl-C or ESC was pressed.
/// -show:  &nbsp;   end
/// -show:  end
static int32 check_input(lua_State* state)
{
    const auto _timeout = optnumber(state, 1, 0);
    const uint32 timeout = (_timeout < 0) ? INFINITE : uint32(_timeout * 1000);

    bool available = false;

    terminal_in* const in = get_lua_terminal_input();
    if (in)
    {
        input_scope is(in);
        available = in->available(timeout);
    }

    lua_pushboolean(state, available);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getcolortable
/// -ver:   1.5.15
/// -ret:   table
/// This returns a table containing the 16 predefined colors in the console's
/// color theme.
///
/// Clink also tries to detect the terminal's background color.  If it's able
/// to, then the returned table also includes either a <code>light = true</code>
/// field or a <code>dark = true</code> field.
///
/// The returned table also includes <code>foreground</code> and
/// <code>background</code> fields which contain the index of the corresponding
/// closest colors in the color table.  If unable to determine the closest
/// colors, then either of these fields may be missing.
///
/// **Note:**  When using Windows Terminal, ConEmu, or other ConPty-based
/// terminals it is currently not possible to get the current color theme until
/// <a href="https://github.com/microsoft/terminal/issues/10639">Terminal#10639</a>
/// gets fixed.  Until that's fixed, this can only return a default color table,
/// which includes a <code>default = true</code> field.  When using the legacy
/// conhost terminal then this is always able to report the current color theme.
static int32 get_color_table(lua_State* state)
{
    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    if (!proc)
        return 0;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFOEX csbix = { sizeof(csbix) };
    if (!GCSBIEx(proc)(h, &csbix))
        return 0;

    lua_createtable(state, 16, 0);

    str<> s;
    for (uint32 i = 0; i < sizeof_array(csbix.ColorTable); ++i)
    {
        COLORREF cr = csbix.ColorTable[i];
        s.format("#%02x%02x%02x", GetRValue(cr), GetGValue(cr), GetBValue(cr));
        lua_pushlstring(state, s.c_str(), s.length());
        lua_rawseti(state, -2, i + 1);
    }

    const uint8 attr = get_console_default_attr();
    const COLORREF cr_fg = csbix.ColorTable[(attr & 0x0f) >> 0];
    const COLORREF cr_bg = csbix.ColorTable[(attr & 0xf0) >> 4];
    const uint8 rgb_fg[3] = { GetRValue(cr_fg), GetGValue(cr_fg), GetBValue(cr_fg) };
    const uint8 rgb_bg[3] = { GetRValue(cr_bg), GetGValue(cr_bg), GetBValue(cr_bg) };
    const int32 index_fg = get_nearest_color(csbix, rgb_fg);
    const int32 index_bg = get_nearest_color(csbix, rgb_bg);

    if (index_fg >= 0)
    {
        lua_pushliteral(state, "foreground");
        lua_pushinteger(state, index_fg + 1);
        lua_rawset(state, -3);
    }
    if (index_bg >= 0)
    {
        lua_pushliteral(state, "background");
        lua_pushinteger(state, index_bg + 1);
        lua_rawset(state, -3);
    }

    const char* theme = nullptr;
    switch (get_console_theme())
    {
    case console_theme::system:     theme = "default"; break;
    case console_theme::dark:       theme = "dark"; break;
    case console_theme::light:      theme = "light"; break;
    }

    if (theme)
    {
        lua_pushstring(state, theme);
        lua_pushboolean(state, true);
        lua_rawset(state, -3);
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.cellcountiter
/// -ver:   1.7.4
/// -arg:   text:string
/// -ret:   iterator
/// This returns an iterator function which steps through
/// <span class="arg">text</span> one glyph at a time.  Each call to the
/// iterator function returns the string for the next glyph, the count of
/// visible character cells that would be consumed when displaying it in the
/// terminal, and a boolean indicating whether the string is an emoji.
///
/// <strong>Note:</strong> This only recognizes emojis if Clink recognizes
/// that the terminal program supports emojis.  The results are "best effort",
/// and may differ from reality depending on the specific OS version, terminal
/// program (and its version), and the input string.  The width prediction is
/// based on the Unicode emoji specification, with accommodations for how
/// Windows Terminal renders certain emoji sequences.
/// -show:  -- UTF8 sample string:
/// -show:  -- Index by glyph:       12                                                   3
/// -show:  -- Unicode character:    A❤️          U+FE0F      ZWJ         🔥              Z
/// -show:  local text            = "A\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x94\xa5Z"
/// -show:  -- Index by byte:        12   3   4   5   6   7   8   9   10  11  12  13  14  15
/// -show:
/// -show:  for str, width, emoji in console.cellcountiter(text) do
/// -show:  &nbsp;   -- Build string showing byte values.
/// -show:  &nbsp;   local bytes = ""
/// -show:  &nbsp;   for i = 1, #str do
/// -show:  &nbsp;       bytes = bytes .. string.format("\\x%02x", str:byte(i, i))
/// -show:  &nbsp;   end
/// -show:  &nbsp;   -- Print the cellcount substring and info about it.
/// -show:  &nbsp;   clink.print(str, width, emoji, bytes)
/// -show:  &nbsp;   -- Print the individual codepoints in the cellcount substring.
/// -show:  &nbsp;   for s, value in unicode.iter(str) do
/// -show:  &nbsp;       bytes = ""
/// -show:  &nbsp;       for i = 1, #s do
/// -show:  &nbsp;           bytes = bytes .. string.format("\\x%02x", s:byte(i, i))
/// -show:  &nbsp;       end
/// -show:  &nbsp;       clink.print("", s, string.format("U+%X", value), bytes)
/// -show:  &nbsp;   end
/// -show:  end
/// -show:
/// -show:  -- The above prints the following:
/// -show:  --      A       1       false   \x41
/// -show:  --              A       U+41    \x41
/// -show:  --      ❤️‍🔥      2       true    \xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x94\xa5
/// -show:  --              ❤       U+2764  \xe2\x9d\xa4
/// -show:  --              ️       U+FE0F  \xef\xb8\x8f
/// -show:  --              ‍        U+200D  \xe2\x80\x8d
/// -show:  --              🔥      U+1F525 \xf0\x9f\x94\xa5
/// -show:  --      Z       1       false   \x5a
/// -show:  --              Z       U+5A    \x5a
static int32 cell_count_iter_aux (lua_State* state)
{
    const char* text = lua_tolstring(state, lua_upvalueindex(1), nullptr);
    const int32 pos = int32(lua_tointeger(state, lua_upvalueindex(2)));
    const char* s = text + pos;

    wcwidth_iter iter(s);
    const int32 c = iter.next();
    if (!c)
        return 0;

    const char* e = iter.get_pointer();

    lua_pushinteger(state, int32(e - text));
    lua_replace(state, lua_upvalueindex(2));

    lua_pushlstring(state, s, size_t(e - s));
    lua_pushinteger(state, iter.character_wcwidth_onectrl());
    lua_pushboolean(state, iter.character_is_emoji());
    return 3;
}
static int32 cell_count_iter(lua_State* state)
{
    const char* s = checkstring(state, 1);
    if (!s)
        return 0;

    lua_settop(state, 1);                   // Reuse the pushed string.
    lua_pushinteger(state, 0);              // Push a position for the next iteration.
    lua_pushcclosure(state, cell_count_iter_aux, 2);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.ellipsify
/// -ver:   1.7.5
/// -arg:   text:string
/// -arg:   limit:integer
/// -arg:   [mode:string]
/// -arg:   [ellipsis:string]
/// -ret:   string, integer
/// Returns as much of <span class="arg">text</span> as fits in
/// <span class="arg">limit</span> screen columns, truncated with either an
/// ellipsis or the optional custom <span class="arg">ellipsis</span> string.
///
/// The optional <span class="arg">mode</span> can be any of the following:
///
/// <table>
/// <tr><th>Value</th><th>Description</th></tr>
/// <tr><td><code>"right"</code></td><td>Truncates from the right end of the input string.</td></tr>
/// <tr><td><code>"left"</code></td><td>Truncates from the left end of the input string.</td></tr>
/// <tr><td><code>"path"</code></td><td>Treats the input string as a file path and keeps any drive specifier, and truncates any directory portion from its left end.</td></tr>
/// </table>
///
/// <strong>Note:</strong> Both <span class="arg">text</span> and/or
/// <span class="arg">ellipsis</span> may include ANSI escape codes.  When the
/// <code>"left"</code> or <code>"path"</code> truncation modes are used, then
/// any ANSI escape code to the left of the ellipsis are preserved, to ensure
/// consistent styling regardless whether truncation occurs.  Any ANSI escape
/// codes in <span class="arg">ellipsis</span> are included as-is, which means
/// it's possible for the caller to give the ellipsis string different styling
/// from the rest of the string.
/// -show:  print(ellipsify("abcdef", 4))
/// -show:  --  abc…
/// -show:
/// -show:  print(ellipsify("abcdef", 4, "right", ".."))
/// -show:  --  ab..
/// -show:
/// -show:  print(ellipsify("abcdef", 4, "right", ""))
/// -show:  --  abcd
/// -show:
/// -show:  print(ellipsify("abcdef", 4, "left"))
/// -show:  --  …def
/// -show:
/// -show:  print(ellipsify("abcdefghijk", 8, "left", "[...]"))
/// -show:  --  [...]ijk
/// -show:
/// -show:  print(ellipsify("abcdefghijk", 4, "left", ""))
/// -show:  --  hijk
/// -show:
/// -show:  print(ellipsify("c:/abcd/wxyz", 8, "path"))
/// -show:  --  c:…/wxyz
/// -show:
/// -show:  print(ellipsify("c:/abcd/wxyz", 8, "path", "..."))
/// -show:  --  c:...xyz
/// -show:
/// -show:  print(ellipsify("c:/abcd/wxyz", 5, "path", ""))
/// -show:  --  c:xyz
static int32 api_ellipsify(lua_State* state)
{
    // Get input string.
    const char* s = checkstring(state, 1);
    if (!s)
        return 0;

    // Get limit.
    const auto limit = checkinteger(state, 2);
    if (!limit.isnum())
        return 0;
    if (limit.get() <= 0)
    {
        lua_pushstring(state, s);
        lua_pushinteger(state, cell_count(s));
        return 2;
    }

    // Get mode (right="Abc...", left="...xyz", path="c:...name").
    ellipsify_mode mode = RIGHT;
    if (!lua_isnoneornil(state, 3))
    {
        const char* m = checkstring(state, 3);
        if (!m)
            return 0;
        if (strcmp(m, "right") == 0) mode = RIGHT;
        else if (strcmp(m, "left") == 0) mode = LEFT;
        else if (strcmp(m, "path") == 0) mode = PATH;
        else mode = INVALID;
        if (mode == INVALID)
        {
            const char* msg = lua_pushfstring(state, "%s expected, got '%s'",
                    "'left', 'right', or 'path'", m);
            return luaL_argerror(state, 2, msg);
        }
    }

    // Get ellipsis character sequence.
    const char* e = optstring(state, 4, nullptr);

    // Perform truncation.
    str<128> out;
    const int32 width = ellipsify_ex(s, limit.get(), mode, out, e);

    lua_pushlstring(state, out.c_str(), out.length());
    lua_pushinteger(state, width);
    return 2;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 set_width(lua_State* state)
{
    static bool s_fudge_verified = false;
    static bool s_fudge_needed = false;

    const auto width = checkinteger(state, 1);
    if (!width.isnum() || width <= 0)
        return 0;

    static HMODULE hmod = GetModuleHandle("kernel32.dll");
    static FARPROC proc_get = GetProcAddress(hmod, "GetConsoleScreenBufferInfoEx");
    static FARPROC proc_set = GetProcAddress(hmod, "SetConsoleScreenBufferInfoEx");
    typedef BOOL (WINAPI* GCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    typedef BOOL (WINAPI* SCSBIEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    if (!proc_get || !proc_set)
        return 0;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFOEX csbix = { sizeof(csbix) };
    if (!GCSBIEx(proc_get)(h, &csbix))
        return 0;

    // GCSBIEx and SCSBIEx use different coordinate systems for srWindow.
    // https://github.com/microsoft/terminal/issues/3698
    // https://github.com/microsoft/terminal/issues/3698#issuecomment-558734017
    ++csbix.srWindow.Right;
    ++csbix.srWindow.Bottom;
    // REVIEW: But sometimes adding 1 overcompensates and increases the height?

    csbix.dwSize.X = width;
    csbix.srWindow.Right = width;
    if (!SCSBIEx(proc_set)(h, &csbix))
        return 0;

    lua_pushboolean(state, true);
    return 1;
}

//------------------------------------------------------------------------------
void console_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int32       (*method)(lua_State*);
    } methods[] = {
        { "scroll",                 &scroll },
        { "cellcount",              &get_cell_count },
        { "plaintext",              &get_plain_text },
        { "explodeansi",            &explode_ansi },
        { "getwidth",               &get_width },
        { "getheight",              &get_height },
        { "getnumlines",            &get_num_lines },
        { "gettop",                 &get_top },
        { "getcursorpos",           &get_cursor_pos },
        { "getlinetext",            &get_line_text },
        { "gettitle",               &get_title },
        { "settitle",               &set_title },
        { "islinedefaultcolor",     &is_line_default_color },
        { "linehascolor",           &line_has_color },
        { "findprevline",           &find_prev_line },
        { "findnextline",           &find_next_line },
        { "readinput",              &read_input },
        { "checkinput",             &check_input },
        { "getcolortable",          &get_color_table },
        { "cellcountiter",          &cell_count_iter },
        { "ellipsify",              &api_ellipsify },
        // UNDOCUMENTED; internal use only.
        { "__set_width",            &set_width },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "console");
}
