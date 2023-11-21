// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#define NOMINMAX
#define VC_EXTRALEAN
#ifndef BUILD_READLINE
#define WIN32_LEAN_AND_MEAN
#endif

#if defined(DEBUG) && defined(_MSC_VER)
#define USE_MEMORY_TRACKING
#define INCLUDE_CALLSTACKS
#define USE_RTTI
#endif

//------------------------------------------------------------------------------
// Define FISH_ARROW_KEYS to make arrow keys in clink-select-complete move as in
// fish shell completion.  Otherwise they move as in powershell completion.
#define FISH_ARROW_KEYS

//------------------------------------------------------------------------------
// Define to use "..." rather than "…" when truncating things.
//#define USE_ASCII_ELLIPSIS

//------------------------------------------------------------------------------
// Define to show vertical scrollbars in clink-select-complete and popup lists.
#define SHOW_VERT_SCROLLBARS

//------------------------------------------------------------------------------
// Debugging options.
#ifdef DEBUG
//#define TRACE_ASSERT_STACK
//#define SHOW_DISPLAY_GENERATION
//#define DEBUG_SUGGEST
//#define DEBUG_RESOLVEIMPL
//#define USE_OS_UTF_CONVERSION
//#define REPORT_READLINE_UNDO_LIST_LEAKS
#endif

//------------------------------------------------------------------------------
// Define this to add callstacks with `debug.log_terminal`.
//#define LOG_OUTPUT_CALLSTACKS

//------------------------------------------------------------------------------
// Define this to run `pushd` repeatedly and expose the directory stack.
// However, it relys on writing temporary batch scripts, which adds possible
// failure points and performance degradation.
//#define CAPTURE_PUSHD_STACK

//------------------------------------------------------------------------------
// Define this to only load Lua scripts whose long name has an exact ".lua"
// extension.  This counteracts 8.3 short names compatibility in Windows.
// It's disabled for consistency with everything else that uses "*.lua".
//#define LUA_FILTER_8DOT3_SHORT_NAMES

//------------------------------------------------------------------------------
// Define this to attempt to automatically choose an appropriate default for
// `color.suggestion` based on the current console color table.  This can't be
// enabled by default because it requires the terminal to support 8-bit or
// 24-bit color; some terminals don't, and there's no way to check whether a
// terminal supports them.  Maybe once Windows Terminal supports returning the
// console color table then this could be enabled for Windows Terminal.
// https://github.com/microsoft/terminal/issues/10639
//#define AUTO_DETECT_CONSOLE_COLOR_THEME

//------------------------------------------------------------------------------
// Clink doesn't support rl_byte_oriented mode.  Defining it to 0 lets the
// compiler optimize away any code specific to that mode.
#define rl_byte_oriented    (0)
