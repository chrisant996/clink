// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "prompt.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/os.h>
#include <lib/line_buffer.h>
#include "lua_script_loader.h"
#include "lua_state.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>

//------------------------------------------------------------------------------
extern "C" int32 is_CJK_codepage(UINT cp);
extern line_buffer* g_rl_buffer;

//------------------------------------------------------------------------------
#define MR(x)                        L##x L"\x08"
static const wchar_t g_prompt_tag[]         = L"@CLINK_PROMPT";
static const wchar_t g_prompt_tag_hidden[]  = MR("C") MR("L") MR("I") MR("N") MR("K") MR(" ");
struct prompt_tag
{
    const wchar_t*  tag;
    int32           len;
};
static const prompt_tag g_prompt_tags[] =
{
    { g_prompt_tag_hidden, sizeof_array(g_prompt_tag_hidden) - 1 },
    { g_prompt_tag, sizeof_array(g_prompt_tag) - 1 },
};
static_assert(sizeof_array(g_prompt_tag) == 14, "unexpected size of g_prompt_tag"); // Should be number of characters plus 1 for terminator.
static_assert(sizeof_array(g_prompt_tag_hidden) == 13, "unexpected size of g_prompt_tag_hidden"); // Should be number of characters plus 1 for terminator.
#undef MR



//------------------------------------------------------------------------------
static bool are_extensions_enabled()
{
    static bool s_extensions = true;
    static bool s_initialized = false;

    if (!s_initialized)
    {
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

    return s_extensions;
}

//------------------------------------------------------------------------------
static void make_version_string(str_base& out)
{
    out.clear();

    str<> ver;
    os::make_version_string(ver);

    wstr<> tmp(ver.c_str());

    LPWSTR buffer = nullptr;
    DWORD_PTR arguments[2] = { DWORD_PTR(tmp.c_str()) };
    const DWORD flags = FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_ARGUMENT_ARRAY;
    FormatMessageW(flags, nullptr, 0x2350,
                    0, (LPWSTR)&buffer,          // Cast for FORMAT_MESSAGE_ALLOCATE_BUFFER.
                    0, (va_list*)&arguments);    // Cast for FORMAT_MESSAGE_ARGUMENT_ARRAY.

    if (buffer)
        out = buffer;
    else
        out = ver.c_str();

    if (buffer)
        LocalFree(buffer);
}



//------------------------------------------------------------------------------
class locale_info
{
public:
    void                init();
    void                format_time(const SYSTEMTIME& systime, str_base& out);
    void                format_date(const SYSTEMTIME& systime, str_base& out);
private:
    void                init(LCTYPE type, str_base& out, const char* def);
private:
    bool                m_initialized = false;
    char                m_date_order = 0;
    bool                m_add_weekday = true;
    LCID                m_lcid;
    str<4>              m_time_sep;
    str<4>              m_date_sep;
    str<4>              m_decimal;
    str<8>              m_weekdays[7];
    wstr<16>            m_short_date;
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

        str<> tmp;
        init(LOCALE_IDATE, tmp, "0");
        switch (atoi(tmp.c_str()))
        {
        default:    m_date_order = 0; break;
        case 1:     m_date_order = 1; break;
        case 2:     m_date_order = 2; break;
        }

        if (are_extensions_enabled())
        {
            init(LOCALE_SSHORTDATE, tmp, "");

            // Adjust the short date picture to include leading zeros for day
            // and month numbers, and to abbreviate day and month names.
            str<> fmt;
            bool in_quote = false;
            for (const char* p = tmp.c_str(); *p;)
            {
                if (*p == '"')
                {
                    in_quote = !in_quote;
                }
                else if (!in_quote && (*p == 'd' || *p == 'M'))
                {
                    const char ch = *p;
                    const char* const start = p;
                    int32 c = 0;
                    while (*p == ch)
                        c++, p++;
                    if (c == 1)
                        fmt.concat(start, 1);   // Insert an extra => 'dd' or 'MM'.
                    else if (c == 4)
                        c--;                    // Remove one => 'ddd' or 'MMM'.
                    fmt.concat(start, c);
                    // CMD only adds the weekday name if the picture does NOT
                    // include 'dd', 'ddd', or 'dddd'.  It seems like it would
                    // treat 'd' and 'dd' the same, but in practice it doesn't.
                    if (c > 1)
                        m_add_weekday = false;
                    continue;
                }
                fmt.concat(p, 1);
                p++;
            }

            m_short_date = fmt.c_str();
        }
    }
}

//------------------------------------------------------------------------------
void locale_info::format_time(const SYSTEMTIME& systime, str_base& out)
{
    init();

    out.format("%2d%s%02d%s%02d%s%02d",
               systime.wHour, m_time_sep.c_str(),
               systime.wMinute, m_time_sep.c_str(),
               systime.wSecond, m_decimal.c_str(),
               systime.wMilliseconds / 10);
}

//------------------------------------------------------------------------------
void locale_info::format_date(const SYSTEMTIME& systime, str_base& out)
{
    init();

    bool add_weekday = true;
    str<> tmp;

    if (are_extensions_enabled() && m_short_date.length())
    {
        WCHAR buffer[128];
        if (!GetDateFormatW(m_lcid, 0, &systime, m_short_date.c_str(), buffer, sizeof_array(buffer)))
            goto fallback;

        add_weekday = m_add_weekday;
        tmp = buffer;
    }
    else
    {
fallback:
        int32 a, b, c;
        switch (m_date_order)
        {
        default:    // month, day, year
            a = systime.wMonth;
            b = systime.wDay;
            c = systime.wYear;
            break;
        case 1:     // day, month, year
            a = systime.wDay;
            b = systime.wMonth;
            c = systime.wYear;
            break;
        case 2:     // year, month, day
            a = systime.wYear;
            b = systime.wMonth;
            c = systime.wDay;
            break;
        }

        tmp.format("%02d%s%02d%s%02d",
                   a, m_date_sep.c_str(),
                   b, m_date_sep.c_str(),
                   c );
    }

    if (m_add_weekday)
    {
        out.clear();
        // East Asian multibyte code pages put the weekday last.
        const UINT cp = GetConsoleOutputCP();
        if (is_CJK_codepage(cp))
            out << tmp.c_str() << " " << m_weekdays[systime.wDayOfWeek].c_str();
        else
            out << m_weekdays[systime.wDayOfWeek].c_str() << " " << tmp.c_str();
    }
    else
    {
        out = tmp.c_str();
    }
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
void prompt::set(const wchar_t* chars, int32 char_count)
{
    clear();

    if (chars == nullptr)
        return;

    if (char_count <= 0)
        char_count = int32(wcslen(chars));

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
void tagged_prompt::set(const wchar_t* chars, int32 char_count)
{
    clear();

    if (int32 tag_length = is_tagged(chars, char_count))
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

    int32 length = int32(wcslen(value));
    length += int32(wcslen(g_prompt_tag_hidden));

    m_data = (wchar_t*)malloc(sizeof(*m_data) * (length + 1));
    wcscpy(m_data, g_prompt_tag_hidden);
    wcscat(m_data, value);
}

//------------------------------------------------------------------------------
int32 tagged_prompt::is_tagged(const wchar_t* chars, int32 char_count)
{
    if (char_count <= 0)
        char_count = int32(wcslen(chars));

    // For each accepted tag...
    for (int32 i = 0; i < sizeof_array(g_prompt_tags); ++i)
    {
        const prompt_tag& tag = g_prompt_tags[i];
        if (tag.len > char_count)
            continue;

        // Found a match? Store it the prompt, minus the tag.
        if (wcsncmp(chars, tag.tag, tag.len) == 0)
            return tag.len;
    }

    return 0;
}



//------------------------------------------------------------------------------
bool prompt_filter::s_filtering = false;
bool prompt_filter::s_transient_filtering = false;

//------------------------------------------------------------------------------
prompt_filter::prompt_filter(lua_state& lua)
: m_lua(lua)
{
}

//------------------------------------------------------------------------------
// For unit tests.
bool prompt_filter::filter(const char* in, str_base& out)
{
    str<16> dummy;
    return filter(in, "", out, dummy);
}

//------------------------------------------------------------------------------
bool prompt_filter::filter(const char* in, const char* rin, str_base& out, str_base& rout, bool transient, bool final)
{
    lua_State* state = m_lua.get_state();

    int32 top = lua_gettop(state);

    // Call Lua to filter prompt
    lua_getglobal(state, "clink");
    if (transient)
        lua_pushliteral(state, "_filter_transient_prompt");
    else
        lua_pushliteral(state, "_filter_prompt");
    lua_rawget(state, -2);

    lua_pushstring(state, in);
    lua_pushstring(state, rin);
    if (g_rl_buffer && transient)
    {
        lua_pushlstring(state, g_rl_buffer->get_buffer(), g_rl_buffer->get_length());
        lua_pushinteger(state, g_rl_buffer->get_cursor());
        lua_pushboolean(state, final);
    }
    else
    {
        lua_pushnil(state);
        lua_pushnil(state);
        lua_pushnil(state);
    }

    rollback<bool> rb1(s_filtering, true);
    rollback<bool> rb2(s_transient_filtering, transient);
    if (m_lua.pcall(state, 5, 3) != 0)
    {
        lua_settop(state, top);
        return !transient;
    }

    // Collect the filtered prompt.
    const char* prompt = lua_tostring(state, -3);
    const char* rprompt = lua_tostring(state, -2);
    const bool ok = lua_toboolean(state, -1);
    out = prompt;
    rout = rprompt;

    lua_settop(state, top);
    return ok && (!transient || (prompt && rprompt));
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
    uint32 length = cursorXy.X;
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
    expand_prompt_codes(env.c_str(), rout, expand_prompt_flags::single_line);
}

//------------------------------------------------------------------------------
void prompt_utils::get_transient_prompt(str_base& out)
{
    str<> env;
    os::get_env("clink_transient_prompt", env);
    if (env.empty())
        env = "$g";
    expand_prompt_codes(env.c_str(), out, expand_prompt_flags::none);
}

//------------------------------------------------------------------------------
void prompt_utils::get_transient_rprompt(str_base& rout)
{
    str<> env;
    os::get_env("clink_transient_rprompt", env);
    expand_prompt_codes(env.c_str(), rout, expand_prompt_flags::single_line);
}

//------------------------------------------------------------------------------
bool prompt_utils::expand_prompt_codes(const char* in, str_base& out, expand_prompt_flags flags)
{
    if (!in || !*in)
        return false;

    str<> tmp;
    locale_info loc;
    const bool single_line = ((flags & expand_prompt_flags::single_line) == expand_prompt_flags::single_line);
    int32 num_plus = 0;

    str_iter iter(in);
    bool trim_right = false;
    while (iter.more())
    {
        const char* ptr = iter.get_pointer();
        uint32 c = iter.next();

        if (single_line && (c == '\r' || c == '\n'))
            break;

        trim_right = false;

        if (c != '$')
        {
            out.concat(ptr, int32(iter.get_pointer() - ptr));
            continue;
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
                loc.format_date(systime, tmp);
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
                out.truncate(uint32(end - out.c_str()));
            }
            break;
        case 'M':   case 'm':
            // Right side prompt trims trailing spaces if it ends with $M
            // (single_line corresponds to right side prompt).
            trim_right = single_line;
            if (are_extensions_enabled())
            {
                os::get_current_dir(tmp);
                if (!tmp.length())
                    break;
                if (!os::get_net_connection_name(tmp.c_str(), tmp))
                    out << "Unknown ";
                else if (tmp.length())
                    out << tmp.c_str() << " ";
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
                loc.format_time(systime, tmp);
                out << tmp;
            }
            break;
        case 'V':   case 'v':
            {
                make_version_string(tmp);
                out << tmp;
            }
            break;
        case '+':
            if ((flags & expand_prompt_flags::omit_pushd) == expand_prompt_flags::omit_pushd)
            {
                if (num_plus++)
                    return false;
            }
            else
            {
                for (int32 depth = os::get_pushd_depth(); depth-- > 0;)
                    out.concat("+", 1);
            }
            break;
        }
    }

    if (trim_right)
    {
        while (true)
        {
            uint32 len = out.length();
            if (!len)
                break;
            len--;
            if (out.c_str()[len] != ' ')
                break;
            out.truncate(len);
        }
    }

    return true;
}
