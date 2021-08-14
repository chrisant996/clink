// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "prompt.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/os.h>
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
class locale_info
{
public:
    void                init();
    void                format_time(bool extensions, const SYSTEMTIME& systime, str_base& out);
    void                format_date(bool extensions, const SYSTEMTIME& systime, str_base& out);
private:
    void                init(LCTYPE type, str_base& out, const char* def);
private:
    bool                m_initialized = false;
    LCID                m_lcid;
    str<16>             m_time_sep;
    str<16>             m_date_sep;
    str<16>             m_decimal;
    str<32>             m_weekdays[7];
};

//------------------------------------------------------------------------------
void locale_info::init()
{
    if (!m_initialized)
    {
        m_initialized = true;

        m_lcid = GetUserDefaultLCID();

        init(LOCALE_STIME, m_time_sep, ":");
        init(LOCALE_SDATE, m_date_sep, "/");
        init(LOCALE_SDECIMAL, m_decimal, ".");

        init(LOCALE_SABBREVDAYNAME7, m_weekdays[0], "Sun");
        init(LOCALE_SABBREVDAYNAME1, m_weekdays[1], "Mon");
        init(LOCALE_SABBREVDAYNAME2, m_weekdays[2], "Tue");
        init(LOCALE_SABBREVDAYNAME3, m_weekdays[3], "Wed");
        init(LOCALE_SABBREVDAYNAME4, m_weekdays[4], "Thu");
        init(LOCALE_SABBREVDAYNAME5, m_weekdays[5], "Fri");
        init(LOCALE_SABBREVDAYNAME6, m_weekdays[6], "Sat");
    }
}

//------------------------------------------------------------------------------
void locale_info::format_time(bool extensions, const SYSTEMTIME& systime, str_base& out)
{
    init();
    out.format("%2d%s%02d%s%02d%s%02d",
               systime.wHour, m_time_sep.c_str(),
               systime.wMinute, m_time_sep.c_str(),
               systime.wSecond, m_decimal.c_str(),
               systime.wMilliseconds / 10);
}

//------------------------------------------------------------------------------
void locale_info::format_date(bool extensions, const SYSTEMTIME& systime, str_base& out)
{
    init();
    out.format("%s %02d%s%02d%s%4d",
               m_weekdays[systime.wDayOfWeek].c_str(),
               systime.wMonth, m_date_sep.c_str(),
               systime.wDay, m_date_sep.c_str(),
               systime.wYear);
}

//------------------------------------------------------------------------------
void locale_info::init(LCTYPE type, str_base& out, const char* def)
{
    WCHAR value[128];
    if (GetLocaleInfoW(m_lcid, type, value, sizeof_array(value)))
        out = value;
    else
        out = def;
}



//------------------------------------------------------------------------------
static class delay_load_mpr
{
public:
                        delay_load_mpr();
    bool                init();
    DWORD               WNetGetConnectionW(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    HMODULE             m_hlib = 0;
    union
    {
        FARPROC         proc[1];
        struct
        {
            DWORD (WINAPI* WNetGetConnectionW)(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength);
        };
    } m_procs;
} s_mpr;

//------------------------------------------------------------------------------
delay_load_mpr::delay_load_mpr()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_mpr::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        m_hlib = LoadLibrary("mpr.dll");
        if (m_hlib)
            m_procs.proc[0] = GetProcAddress(m_hlib, "WNetGetConnectionW");
        m_ok = !!m_procs.WNetGetConnectionW;
    }

    return m_ok;
}

//------------------------------------------------------------------------------
DWORD delay_load_mpr::WNetGetConnectionW(LPCWSTR lpLocalName, LPWSTR lpRemoteName, LPDWORD lpnLength)
{
    if (!m_procs.WNetGetConnectionW)
        return ERROR_NOT_SUPPORTED;
    return m_procs.WNetGetConnectionW(lpLocalName, lpRemoteName, lpnLength);
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
prompt_filter::prompt_filter(lua_state& lua)
: m_lua(lua)
{
    lua_load_script(lua, app, prompt);
}

//------------------------------------------------------------------------------
void prompt_filter::filter(const char* in, str_base& out)
{
    str<16> dummy;
    filter(in, "", out, dummy);
}

//------------------------------------------------------------------------------
void prompt_filter::filter(const char* in, const char* rin, str_base& out, str_base& rout)
{
    lua_State* state = m_lua.get_state();

    int top = lua_gettop(state);

    // Call Lua to filter prompt
    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_filter_prompt");
    lua_rawget(state, -2);

    lua_pushstring(state, in);
    lua_pushstring(state, rin);

    if (m_lua.pcall(state, 2, 2) != 0)
    {
        puts(lua_tostring(state, -1));
        lua_pop(state, 2);
        return;
    }

    // Collect the filtered prompt.
    const char* prompt = lua_tostring(state, -2);
    const char* rprompt = lua_tostring(state, -1);
    out = prompt;
    rout = rprompt;

    lua_settop(state, top);
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

//------------------------------------------------------------------------------
void prompt_utils::get_rprompt(str_base& rout)
{
    str<> env;
    os::get_env("clink_rprompt", env);
    expand_prompt_codes(env.c_str(), rout, true/*single_line*/);
}

//------------------------------------------------------------------------------
void prompt_utils::expand_prompt_codes(const char* in, str_base& out, bool single_line)
{
    if (!in || !*in)
        return;

    str<> tmp;
    locale_info loc;
    static bool s_extensions = true;
    static bool s_initialized = false;

    str_iter iter(in);
    while (iter.more())
    {
        const char* ptr = iter.get_pointer();
        unsigned int c = iter.next();

        if (single_line && (c == '\r' || c == '\n'))
            break;

        if (c != '$')
        {
            out.concat(ptr, int(iter.get_pointer() - ptr));
            continue;
        }

        if (!s_initialized)
        {
            // Defer the cost of checking for extensions until a $ code is
            // encountered.
            s_initialized = true;
            DWORD type;
            WCHAR value[MAX_PATH];
            DWORD len = sizeof(value);
            HKEY hives[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
            for (HKEY hive : hives)
            {
                HKEY hkey = 0;
                if (RegOpenKeyEx(hive, "Software\\Microsoft\\Command Processor", 0, MAXIMUM_ALLOWED, &hkey))
                    continue;
                len = sizeof(value);
                if (!RegQueryValueEx(hkey, "EnableExtensions", NULL, &type, LPBYTE(&value), &len))
                {
                    switch (type)
                    {
                    case REG_DWORD: s_extensions = !!*(reinterpret_cast<const DWORD*>(&value)); break;
                    case REG_SZ: s_extensions = _wtoi(value) == 1; break;
                    }
                }
                RegCloseKey(hkey);
            }
        }

        c = iter.next();
        if (single_line && c == '_')
            break;

        switch (c)
        {
        case 'A':   case 'a':   out << "&"; break;
        case 'B':   case 'b':   out << "|"; break;
        case 'C':   case 'c':   out << "("; break;
        case 'E':   case 'e':   out << "\x1b"; break;
        case 'F':   case 'f':   out << ")"; break;
        case 'G':   case 'g':   out << ">"; break;
        case 'L':   case 'l':   out << "<"; break;
        case 'Q':   case 'q':   out << "="; break;
        case 'S':   case 's':   out << " "; break;
        case '_':               out << "\r\n"; break;
        case '$':               out << "$"; break;

        case 'D':   case 'd':
            {
                SYSTEMTIME systime;
                GetLocalTime(&systime);
                loc.format_date(s_extensions, systime, tmp);
                out << tmp;
            }
            break;
        case 'H':   case 'h':
            {
                // CMD's native $H processing for PROMPT seems to have strange
                // behaviors depending on what is being deleted.  Clink will
                // interpret $H as deleting the preceding UTF32 character.  In
                // other words it will handle surrogate pairs, but doesn't try
                // to deal with complexities like zero width joiners.
                //
                // For now this scans from the beginning of the string to find
                // the width of the last UTF32 character in bytes.  Using many
                // $H in a prompt string could degrade performance.  It's
                // probably rarely and sparingly used, so this should suffice.
                str_iter trim(out.c_str(), out.length());
                const char* end = out.c_str();
                while (trim.more())
                {
                    end = trim.get_pointer();
                    trim.next();
                }
                out.truncate(static_cast<unsigned int>(end - out.c_str()));
            }
            break;
        case 'M':   case 'm':
            if (s_extensions && s_mpr.init())
            {
                os::get_current_dir(tmp);
                if (!tmp.length())
                    break;

                WCHAR drive[4];
                drive[0] = tmp.c_str()[0];
                drive[1] = tmp.c_str()[1];
                drive[2] = '\\';
                drive[3] = '\0';
                if (GetDriveTypeW(drive) != DRIVE_REMOTE)
                    break;

                drive[2] = '\0';
                WCHAR remote[MAX_PATH];
                DWORD len = sizeof_array(remote);
                DWORD err = s_mpr.WNetGetConnectionW(drive, remote, &len);

                switch (err)
                {
                case NO_ERROR:
                    to_utf8(out, remote);
                    out << " ";
                    break;
                case ERROR_NOT_CONNECTED:
                case ERROR_NOT_SUPPORTED:
                    break;
                default:
                    out << "Unknown";
                    break;
                }
            }
            break;
        case 'N':   case 'n':
            {
                os::get_current_dir(tmp);
                if (tmp.length())
                    out.concat(tmp.c_str(), 1);
            }
            break;
        case 'P':   case 'p':
            os::get_current_dir(tmp);
            out << tmp;
            break;
        case 'T':   case 't':
            {
                SYSTEMTIME systime;
                GetLocalTime(&systime);
                loc.format_time(s_extensions, systime, tmp);
                out << tmp;
            }
            break;

        // Not supported.
        case 'V':   case 'v':   break;
        case '+':               break;
        }
    }
}
