// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "terminal/scroll.h"
#include "terminal/screen_buffer.h" // for set_console_title
#include "terminal/printer.h"
#include "terminal/find_line.h"
#include "terminal/ecma48_iter.h"
#include "terminal/terminal.h"
#include "terminal/terminal_in.h"
#include "terminal/terminal_helpers.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

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
static int scroll(lua_State* state)
{
    bool isnum;
    const char* mode = checkstring(state, 1);
    int amount = checkinteger(state, 2, &isnum);
    if (!mode || !isnum)
        return 0;

    SCRMODE scrmode = SCR_BYLINE;
    if (stricmp(mode, "page") == 0)
        scrmode = SCR_BYPAGE;
    else if (stricmp(mode, "end") == 0)
        scrmode = SCR_TOEND;
    else if (stricmp(mode, "absolute") == 0)
    {
        scrmode = SCR_ABSOLUTE;
        amount--;
    }

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    int scrolled = ScrollConsoleRelative(h, amount, scrmode);
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
/// Note: backspace characters and line endings are counted as visible character
/// cells and will skew the resulting count.
static int get_cell_count(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    unsigned int cells = 0;
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
/// Note: backspace characters and line endings are counted as visible character
/// cells and will skew the resulting count.
static int get_plain_text(lua_State* state)
{
    const char* in = checkstring(state, 1);
    if (!in)
        return 0;

    str<> out;
    unsigned int cells = 0;
    ecma48_processor(in, &out, &cells, ecma48_processor_flags::plaintext);
    lua_pushlstring(state, out.c_str(), out.length());
    lua_pushinteger(state, cells);
    return 2;
}

//------------------------------------------------------------------------------
/// -name:  console.getwidth
/// -ver:   1.1.20
/// -ret:   integer
/// Returns the width of the console screen buffer in characters.
static int get_width(lua_State* state)
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
static int get_height(lua_State* state)
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
static int get_num_lines(lua_State* state)
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
static int get_top(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.srWindow.Top + 1);
    return 1;
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
static int get_line_text(lua_State* state)
{
    if (!g_printer)
        return 0;

    bool isnum;
    SHORT line = checkinteger(state, 1, &isnum) - 1;
    if (!isnum)
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

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
static int get_title(lua_State* state)
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
static int set_title(lua_State* state)
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
static int is_line_default_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    bool isnum;
    SHORT line = checkinteger(state, 1, &isnum) - 1;
    if (!isnum)
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

    int result = g_printer->is_line_default_color(line);

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
static int line_has_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    bool isnum;
    SHORT line = checkinteger(state, 1, &isnum) - 1;
    bool has_attrs = lua_isnumber(state, 2) || lua_istable(state, 2);
    if (!has_attrs)
        luaL_argerror(state, 2, "must be number or table of numbers");
    const char* mask_name = optstring(state, 3, "");
    if (!isnum || !has_attrs || !mask_name)
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

    BYTE mask = 0xff;
    if (mask_name && *mask_name)
    {
        if (strcmp(mask_name, "fore") == 0)         mask = 0x0f;
        else if (strcmp(mask_name, "back") == 0)    mask = 0xf0;
        else if (strcmp(mask_name, "both") == 0)    mask = 0xff;
    }

    int result;

    if (lua_isnumber(state, 2))
    {
        BYTE attr = BYTE(lua_tointeger(state, 2));
        result = g_printer->line_has_color(line, &attr, 1, mask);
    }
    else
    {
        BYTE attrs[32];
        int num_attrs = 0;

        for (int i = 1; num_attrs <= sizeof_array(attrs); i++)
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

static int find_line(lua_State* state, int direction)
{
    if (!g_printer)
        return 0;

    int arg = 1;

    // Starting line number is required.
    bool isnum;
    SHORT starting_line = checkinteger(state, arg, &isnum) - 1;
    if (!isnum)
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT num_lines = GetConsoleNumLines(csbi);

    starting_line = min<int>(starting_line, num_lines);
    starting_line = max<int>(starting_line, 0);
    arg++;

    int distance;
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
            if (token.equals("regex"))
                mode |= find_line_mode::use_regex;
            else if (token.equals("icase"))
                mode |= find_line_mode::ignore_case;
        }
    }

    // Attr? Attrs?
    BYTE attrs[32];
    int num_attrs = 0;
    if (lua_isnumber(state, arg))
    {
        attrs[0] = BYTE(lua_tointeger(state, arg));
        num_attrs = 1;
        arg++;
    }
    else if (lua_istable(state, arg))
    {
        for (int i = 1; num_attrs <= sizeof_array(attrs); i++)
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

    int line_found = g_printer->find_line(starting_line, distance, text, mode, attrs, num_attrs, mask);

    lua_pushinteger(state, line_found + 1);
    return 1;
}

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
static int find_prev_line(lua_State* state)
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
static int find_next_line(lua_State* state)
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
static int read_input(lua_State* state)
{
    bool select = true;
    str<> key;

    const bool no_cursor = (lua_isboolean(state, 1) && lua_toboolean(state, 1));

    terminal term = terminal_create(nullptr, !no_cursor);
    term.in->begin();

    // Get one full input key sequence.
    while (true)
    {
        int k = term.in->read();
        if (k == terminal_in::input_none)
        {
            if (!select)
                break;
            term.in->select();
            select = false;
            continue;
        }

        if (k == terminal_in::input_abort)
            break;

        if (k == terminal_in::input_terminal_resize ||
            k == terminal_in::input_exit)
            continue;

        char c = static_cast<char>(k);
        key.concat_no_truncate(&c, 1);
    }

    term.in->end();
    terminal_destroy(term);

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
/// <strong>Note:</strong> Mouse input is not supported.
/// -show:  if console.checkinput() then
/// -show:  &nbsp;   local key = console.readinput() -- Returns immediately since input is available.
/// -show:  &nbsp;   if key == "\x03" or key == "\x1b[27;27~" or key == "\x1b" then
/// -show:  &nbsp;       -- Ctrl-C or ESC was pressed.
/// -show:  &nbsp;   end
/// -show:  end
static int check_input(lua_State* state)
{
    const DWORD timeout = static_cast<DWORD>(optnumber(state, 1, 0) * 1000);

    terminal term = terminal_create(nullptr, false);
    term.in->begin();

    const bool available = term.in->available(timeout);

    term.in->end();
    terminal_destroy(term);

    lua_pushboolean(state, available);
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int set_width(lua_State* state)
{
    static bool s_fudge_verified = false;
    static bool s_fudge_needed = false;

    const int width = checkinteger(state, 1);
    if (width <= 0)
        return 0;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFOEX csbix = { sizeof(csbix) };
    if (!GetConsoleScreenBufferInfoEx(h, &csbix))
        return 0;

    csbix.dwSize.X = width;
    csbix.srWindow.Right = width;
    if (!SetConsoleScreenBufferInfoEx(h, &csbix))
        return 0;

// BUGBUG:  SetConsoleScreenBufferInfoEx isn't working correctly; sometimes it
// shrinks the height by 1, but sometimes adding 1 overcompensates and increases
// the height.

    lua_pushboolean(state, true);
    return 1;
}

//------------------------------------------------------------------------------
void console_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "scroll",                 &scroll },
        { "cellcount",              &get_cell_count },
        { "plaintext",              &get_plain_text },
        { "getwidth",               &get_width },
        { "getheight",              &get_height },
        { "getnumlines",            &get_num_lines },
        { "gettop",                 &get_top },
        { "getlinetext",            &get_line_text },
        { "gettitle",               &get_title },
        { "settitle",               &set_title },
        { "islinedefaultcolor",     &is_line_default_color },
        { "linehascolor",           &line_has_color },
        { "findprevline",           &find_prev_line },
        { "findnextline",           &find_next_line },
        { "readinput",              &read_input },
        { "checkinput",             &check_input },
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
