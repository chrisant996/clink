// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "prompt.h"

#include <core/base.h>
#include <core/str.h>
#include <lua/lua_script_loader.h>
#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>

//------------------------------------------------------------------------------
#define MR(x)                        L##x L"\x08"
const wchar_t* g_prompt_tag          = L"@CLINK_PROMPT";
const wchar_t* g_prompt_tag_hidden   = MR("C") MR("L") MR("I") MR("N") MR("K") MR(" ");
const wchar_t* g_prompt_tags[]       = { g_prompt_tag_hidden, g_prompt_tag };
#undef MR



//------------------------------------------------------------------------------
prompt::prompt()
: m_data(nullptr)
{
}

//------------------------------------------------------------------------------
prompt::prompt(prompt&& rhs)
: m_data(nullptr)
{
    std::swap(m_data, rhs.m_data);
}

//------------------------------------------------------------------------------
prompt::~prompt()
{
    clear();
}

//------------------------------------------------------------------------------
prompt& prompt::operator = (prompt&& rhs)
{
    clear();
    std::swap(m_data, rhs.m_data);
    return *this;
}

//------------------------------------------------------------------------------
void prompt::clear()
{
    if (m_data != nullptr)
        free(m_data);

    m_data = nullptr;
}

//------------------------------------------------------------------------------
const wchar_t* prompt::get() const
{
    return m_data;
}

//------------------------------------------------------------------------------
void prompt::set(const wchar_t* chars, int char_count)
{
    clear();

    if (chars == nullptr)
        return;

    if (char_count <= 0)
        char_count = int(wcslen(chars));

    m_data = (wchar_t*)malloc(sizeof(*m_data) * (char_count + 1));
    wcsncpy(m_data, chars, char_count);
    m_data[char_count] = '\0';
}

//------------------------------------------------------------------------------
bool prompt::is_set() const
{
    return (m_data != nullptr);
}



//------------------------------------------------------------------------------
void tagged_prompt::set(const wchar_t* chars, int char_count)
{
    clear();

    if (int tag_length = is_tagged(chars, char_count))
        prompt::set(chars + tag_length, char_count - tag_length);
}

//------------------------------------------------------------------------------
void tagged_prompt::tag(const wchar_t* value)
{
    clear();

    // Just set 'value' if it is already tagged.
    if (is_tagged(value))
    {
        prompt::set(value);
        return;
    }

    int length = int(wcslen(value));
    length += int(wcslen(g_prompt_tag_hidden));

    m_data = (wchar_t*)malloc(sizeof(*m_data) * (length + 1));
    wcscpy(m_data, g_prompt_tag_hidden);
    wcscat(m_data, value);
}

//------------------------------------------------------------------------------
int tagged_prompt::is_tagged(const wchar_t* chars, int char_count)
{
    if (char_count <= 0)
        char_count = int(wcslen(chars));

    // For each accepted tag...
    for (int i = 0; i < sizeof_array(g_prompt_tags); ++i)
    {
        const wchar_t* tag = g_prompt_tags[i];
        int tag_length = (int)wcslen(tag);

        if (tag_length > char_count)
            continue;

        // Found a match? Store it the prompt, minus the tag.
        if (wcsncmp(chars, tag, tag_length) == 0)
            return tag_length;
    }

    return 0;
}



//------------------------------------------------------------------------------
prompt_filter::prompt_filter(lua_state& lua)
: m_lua(lua)
{
    lua_load_script(lua, app, prompt);
}

//------------------------------------------------------------------------------
void prompt_filter::filter(const char* in, str_base& out)
{
    lua_State* state = m_lua.get_state();

    // Call Lua to filter prompt
    lua_getglobal(state, "prompt");
    lua_pushliteral(state, "filter");
    lua_rawget(state, -2);

    lua_pushstring(state, in);
    if (lua_pcall(state, 1, 1, 0) != 0)
    {
        puts(lua_tostring(state, -1));
        lua_pop(state, 2);
        return;
    }

    // Collect the filtered prompt.
    const char* prompt = lua_tostring(state, -1);
    out = prompt;

    lua_pop(state, 2);
}



//------------------------------------------------------------------------------
prompt prompt_utils::extract_from_console()
{
    // Find where the cursor is. This will be the end of the prompt to extract.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(handle, &csbi) == FALSE)
        return prompt();

    // Work out prompt length.
    COORD cursorXy = csbi.dwCursorPosition;
    unsigned int length = cursorXy.X;
    cursorXy.X = 0;

    wchar_t buffer[256] = {};
    if (length >= sizeof_array(buffer))
        return prompt();

    // Get the prompt from the terminal.
    DWORD chars_in;
    if (!ReadConsoleOutputCharacterW(handle, buffer, length, cursorXy, &chars_in))
        return prompt();

    buffer[chars_in] = '\0';

    // Wrap in a prompt object and return.
    prompt ret;
    ret.set(buffer);
    return ret;
}
