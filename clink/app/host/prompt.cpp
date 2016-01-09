// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "prompt.h"

#include <core/base.h>
#include <core/str.h>

#include <algorithm>

//------------------------------------------------------------------------------
const char*         find_next_ansi_code(const char*, int*);
void                lua_filter_prompt(char*, int);

//------------------------------------------------------------------------------
#define MR(x)                        L##x L"\x08"
const wchar_t* g_prompt_tag          = L"@CLINK_PROMPT";
const wchar_t* g_prompt_tag_hidden   = MR("C") MR("L") MR("I") MR("N") MR("K") MR(" ");
const wchar_t* g_prompt_tags[]       = { g_prompt_tag_hidden, g_prompt_tag };
#undef MR

//------------------------------------------------------------------------------
static int parse_backspaces(char* prompt, int n)
{
    // This function does not null terminate!

    char* write;
    char* read;

    write = prompt;
    read = prompt;
    while (*read && read < (prompt + n))
    {
        if (*read == '\b')
        {
            if (write > prompt)
            {
                --write;
            }
        }
        else
        {
            *write = *read;
            ++write;
        }

        ++read;
    }

    return (int)(write - prompt);
}

//------------------------------------------------------------------------------
char* filter_prompt(const char* in_prompt)
{
#if MODE4
    // Get the prompt from Readline and pass it to Clink's filter framework
    // in Lua.
    str<256> lua_prompt;
    lua_prompt << in_prompt;
    lua_filter_prompt(lua_prompt.data(), lua_prompt.size());

    // Scan for ansi codes and surround them with Readline's markers for
    // invisible characters.
    static const int buf_size = 0x4000;
    str_base out_prompt((char*)malloc(buf_size), buf_size);

    char* next = lua_prompt.data();
    while (*next)
    {
        int ansi_size;
        char* ansi_code = (char*)find_next_ansi_code(next, &ansi_size);

        int len = parse_backspaces(next, (int)(ansi_code - next));
        out_prompt.concat(next, len);

        if (*ansi_code)
        {
            len = parse_backspaces(ansi_code, ansi_size);

            static const char* tags[] = { "\001", "\002" };
            out_prompt << tags[0];
            out_prompt.concat(ansi_code, len);
            out_prompt << tags[1];
        }

        next = ansi_code + ansi_size;
    }

    return out_prompt.data();
#else
    str_base out_prompt((char*)malloc(0x1000), 0x1000);
    out_prompt << in_prompt;
    return out_prompt.data();
#endif // MODE4
}



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
