// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "terminal/scroll.h"
#include "terminal/screen_buffer.h" // for set_console_title
#include "terminal/printer.h"
#include "terminal/find_line.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

//------------------------------------------------------------------------------
extern "C" int _rl_vis_botlin;
extern "C" int _rl_last_v_pos;

//------------------------------------------------------------------------------
extern printer* g_printer;

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
/// negative is up and positive is down.
/// <tr><td>"page"</td><td>Scrolls by <span class="arg">amount</span> pages;
/// negative is up and positive is down.
/// <tr><td>"end"</td><td>Scrolls to the top if <span class="arg">amount</span>
/// is negative, or to the bottom if positive.
/// <tr><td>"absolute"</td><td>Scrolls to line <span class="arg">amount</span>,
/// from 1 to <a href="#console.getnumlines">console.getnumlines()</a>.
/// </table>
static int scroll(lua_State* state)
{
    if (!lua_isstring(state, 1)) return 0;
    if (!lua_isnumber(state, 2)) return 0;

    const char* mode = lua_tostring(state, 1);
    int amount = int(lua_tointeger(state, 2));

    SCRMODE scrmode = SCR_BYLINE;
    if (stricmp(mode, "page") == 0)
        scrmode = SCR_BYPAGE;
    else if (stricmp(mode, "end") == 0)
        scrmode = SCR_TOEND;
    else if (stricmp(mode, "absolute") == 0)
        scrmode = SCR_ABSOLUTE;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    int scrolled = ScrollConsoleRelative(h, amount, scrmode);
    lua_pushinteger(state, scrolled);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getwidth
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

    if (!lua_isnumber(state, 1))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT line = int(lua_tointeger(state, 1)) - 1;
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
/// -arg:   title:string
/// Sets the console title text.
static int set_title(lua_State* state)
{
    if (!lua_isstring(state, 1))
        return 0;

    const char* title = lua_tostring(state, 1);
    set_console_title(title);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  console.islinedefaultcolor
/// -arg:   line:integer
/// -ret:   boolean
/// Returns whether line number <span class="arg">line</span> uses only the
/// default text color.
static int is_line_default_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT line = int(lua_tointeger(state, 1)) - 1;
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

    if (!lua_isnumber(state, 1))
        return 0;
    if (!lua_isnumber(state, 2) && !lua_istable(state, 2))
        return 0;
    if (!lua_isnil(state, 3) && !lua_isstring(state, 3))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT line = int(lua_tointeger(state, 1)) - 1;
    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

    BYTE mask = 0xff;
    {
        const char* mask_name = lua_tostring(state, 3);
        if (mask_name)
        {
            if (strcmp(mask_name, "fore") == 0)         mask = 0x0f;
            else if (strcmp(mask_name, "back") == 0)    mask = 0xf0;
            else if (strcmp(mask_name, "both") == 0)    mask = 0xff;
        }
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
    if (!lua_isnumber(state, arg))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT num_lines = GetConsoleNumLines(csbi);

    SHORT starting_line = int(lua_tointeger(state, arg)) - 1;
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
    else
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

        if (mask_name)
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

//------------------------------------------------------------------------------
/// -name:  console.findprevline
/// -arg:   starting_line:integer
/// -arg:   [text:string]
/// -arg:   [mode:string]
/// -arg:   [attr:integer]
/// -arg:   [attrs:table of integers]
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
/// The following example provides a pair of <code>find_prev_colored_line</code>
/// and <code>find_next_colored_line</code> functions.  The functions can be
/// bound to keys via the <code>luafunc:</code> macro syntax in a .inputrc file.
/// They scroll the screen buffer to the previous or next line that contains
/// "warn" or "error" colored red or yellow.
/// -show:  local was_top
/// -show:  local found_index
/// -show:
/// -show:  local function reset_found()
/// -show:  &nbsp; was_top = nil
/// -show:  &nbsp; found_index = nil
/// -show:  end
/// -show:
/// -show:  -- Register for the onbeginedit event, to reset the found
/// -show:  -- line number each time a new editing prompt begins.
/// -show:  clink.onbeginedit(reset_found)
/// -show:
/// -show:  -- Searches upwards for a line containing "warn" or "error"
/// -show:  -- colored red or yellow.
/// -show:  function find_prev_colored_line(rl_buffer)
/// -show:  &nbsp; local height = console.getheight()
/// -show:  &nbsp; local cur_top = console.gettop()
/// -show:  &nbsp; local offset = math.modf((height - 1) / 2) -- For vertically centering the found line.
/// -show:
/// -show:  &nbsp; local start
/// -show:  &nbsp; if found_index == nil or cur_top ~= was_top then
/// -show:  &nbsp;   start = cur_top
/// -show:  &nbsp;   was_top = start
/// -show:  &nbsp; else
/// -show:  &nbsp;   start = found_index
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; -- Only search if there's still room to scroll up.
/// -show:  &nbsp; if start - offset > 1 then
/// -show:  &nbsp;   local match = console.findprevline(start - 1, "warn|error", "regex", {4,12,14}, "fore")
/// -show:  &nbsp;   if match ~= nil and match > 0 then
/// -show:  &nbsp;     found_index = match
/// -show:  &nbsp;   end
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; if found_index ~= nil then
/// -show:  &nbsp;   console.scroll("absolute", found_index - offset)
/// -show:  &nbsp;   was_top = console.gettop()
/// -show:  &nbsp; else
/// -show:  &nbsp;   rl_buffer:ding()
/// -show:  &nbsp; end
/// -show:  end
/// -show:
/// -show:  -- Searches downwards for a line containing "warn" or "error"
/// -show:  -- colored red or yellow.
/// -show:  function find_next_colored_line(rl_buffer)
/// -show:  &nbsp; if found_index == nil then
/// -show:  &nbsp;   rl_buffer:ding()
/// -show:  &nbsp;   return
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; local height = console.getheight()
/// -show:  &nbsp; local cur_top = console.gettop()
/// -show:  &nbsp; local offset = math.modf((height - 1) / 2)
/// -show:
/// -show:  &nbsp; local start
/// -show:  &nbsp; if cur_top ~= was_top then
/// -show:  &nbsp;     start = cur_top + height - 1
/// -show:  &nbsp;     was_top = cur_top
/// -show:  &nbsp; else
/// -show:  &nbsp;     start = found_index
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; -- Only search if there's still room to scroll down.
/// -show:  &nbsp; local bottom = console.getnumlines()
/// -show:  &nbsp; if start - offset + height - 1 < bottom then
/// -show:  &nbsp;   local match = console.findnextline(start + 1, "warn|error", "regex", {4,12,14}, "fore")
/// -show:  &nbsp;   if match ~= nil and match > 0 then
/// -show:  &nbsp;     found_index = match
/// -show:  &nbsp;   end
/// -show:  &nbsp; end
/// -show:
/// -show:  &nbsp; if found_index ~= nil then
/// -show:  &nbsp;   console.scroll("absolute", found_index - offset)
/// -show:  &nbsp;   was_top = console.gettop()
/// -show:  &nbsp; else
/// -show:  &nbsp;   rl_buffer:ding()
/// -show:  &nbsp; end
/// -show:  end
static int find_prev_line(lua_State* state)
{
    return find_line(state, -1);
}

//------------------------------------------------------------------------------
/// -name:  console.findnextline
/// -arg:   starting_line:integer
/// -arg:   [text:string]
/// -arg:   [mode:string]
/// -arg:   [attr:integer]
/// -arg:   [attrs:table of integers]
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
void console_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "scroll",                 &scroll },
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
