// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/str_tokeniser.h>

#include <winnls.h>

//------------------------------------------------------------------------------
static class delay_load_normaliz
{
public:
                        delay_load_normaliz();
    bool                init();
    int                 NormalizeString(NORM_FORM NormForm, LPCWSTR lpSrcString, int cwSrcLength, LPWSTR lpDstString, int cwDstLength);
    BOOL                IsNormalizedString(NORM_FORM NormForm, LPCWSTR lpString, int cwLength);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    HMODULE             m_hlib = 0;
    union
    {
        FARPROC         proc[2];
        struct
        {
            int (WINAPI* NormalizeString)(NORM_FORM NormForm, LPCWSTR lpSrcString, int cwSrcLength, LPWSTR lpDstString, int cwDstLength);
            BOOL (WINAPI* IsNormalizedString)(NORM_FORM NormForm, LPCWSTR lpString, int cwLength);
        };
    } m_procs;
} s_normaliz;

//------------------------------------------------------------------------------
delay_load_normaliz::delay_load_normaliz()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_normaliz::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        m_hlib = LoadLibrary("normaliz.dll");
        if (m_hlib)
        {
            m_procs.proc[0] = GetProcAddress(m_hlib, "NormalizeString");
            m_procs.proc[1] = GetProcAddress(m_hlib, "IsNormalizedString");
        }
        m_ok = !!m_procs.proc[0] && !!m_procs.proc[1];
    }

    return m_ok;
}

//------------------------------------------------------------------------------
int delay_load_normaliz::NormalizeString(NORM_FORM NormForm, LPCWSTR lpSrcString, int cwSrcLength, LPWSTR lpDstString, int cwDstLength)
{
    if (init() && !m_procs.NormalizeString)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    return m_procs.NormalizeString(NormForm, lpSrcString, cwSrcLength, lpDstString, cwDstLength);
}

//------------------------------------------------------------------------------
BOOL delay_load_normaliz::IsNormalizedString(NORM_FORM NormForm, LPCWSTR lpString, int cwLength)
{
    if (init() && !m_procs.IsNormalizedString)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    return m_procs.IsNormalizedString(NormForm, lpString, cwLength);
}



//------------------------------------------------------------------------------
#define in_range(lo, c, hi)     ((unsigned)((c) - (lo)) <= unsigned((hi) - (lo)))

//------------------------------------------------------------------------------
static bool is_combining_mark(int c)
{
    return (in_range(0x0300, c, 0x36f) ||
            in_range(0x20d0, c, 0x20ef) ||
            in_range(0x3099, c, 0x309a));
}



//------------------------------------------------------------------------------
/// -name:  unicode.normalize
/// -ver:   1.3.26
/// -arg:   form:integer
/// -arg:   text:string
/// -ret:   string [, integer]
/// Transforms the <span class="arg">text</span> according to the Unicode
/// normalization <span class="arg">form</span>:
///
/// <ul>
/// <li><code>1</code> is Unicode normalization form C, canonical composition.
/// Transforms each base character + combining characters into the precomposed
/// equivalent.  For example, A + umlaut becomes Ä.
/// <li><code>2</code> is Unicode normalization form D, canonical decomposition.
/// Transforms each precomposed character into base character + combining
/// characters.  For example, Ä becomes A + umlaut.
/// <li><code>3</code> is Unicode normalization form KC, compatibility
/// composition.  Transforms each base character + combining characters into the
/// precomposed equivalent, and transforms compatibility characters into their
/// equivalents.  For example, A + umlaut + ligature for "fi" becomes Ä + f + i.
/// <li><code>4</code> is Unicode normalization form KD, compatibility
/// decomposition.  Transforms each precomposed character into base character +
/// combining characters, and transforms compatibility characters to their
/// equivalents.  For example, Ä + ligature for "fi" becomes A + umlaut + f + i.
/// </ul>
///
/// If successful, the resulting string is returned.
///
/// If unsuccessful, both the original string and an error code are returned.
static int normalize(lua_State* state)
{
    bool isnum;
    int form = checkinteger(state, 1, &isnum);
    const char* text = checkstring(state, 2);
    if (!isnum || !text)
        return 0;

    if (form < 1 || form > 4)
    {
        luaL_argerror(state, 1, "must be an integer 1 to 4");
        return 0;
    }

    if (!*text)
    {
        lua_pushstring(state, text);
        return 1;
    }

    NORM_FORM norm;
    switch (form)
    {
    case 1: norm = NormalizationC; break;
    case 2: norm = NormalizationD; break;
    case 3: norm = NormalizationKC; break;
    case 4: norm = NormalizationKD; break;
    default: assert(false); return 0;
    }

    wstr_moveable in(text);
    wstr_moveable tmp;

    int estimate = s_normaliz.NormalizeString(norm, in.c_str(), -1, nullptr, 0);
    if (estimate <= 0)
    {
failed:
        DWORD err = GetLastError();
        lua_pushstring(state, text);
        lua_pushinteger(state, err);
        return 2;
    }

    while (estimate > 0)
    {
        if (!tmp.reserve(estimate))
        {
            tmp.free();
            SetLastError(ERROR_OUTOFMEMORY);
            goto failed;
        }

        estimate = s_normaliz.NormalizeString(norm, in.c_str(), -1, tmp.data(), tmp.size());
        if (estimate >= 0)
            break;

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            goto failed;

        estimate = 0 - estimate;
    }

    str_moveable out(tmp.c_str());
    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  unicode.isnormalized
/// -ver:   1.3.26
/// -arg:   form:integer
/// -arg:   text:string
/// -ret:   boolean [, integer]
/// Returns whether <span class="arg">text</span> is already normalized
/// according to the Unicode normalization <span class="arg">form</span>:
///
/// <ul>
/// <li><code>1</code> is Unicode normalization form C, canonical composition.
/// Transforms each base character + combining characters into the precomposed
/// equivalent.  For example, A + umlaut becomes Ä.
/// <li><code>2</code> is Unicode normalization form D, canonical decomposition.
/// Transforms each precomposed character into base character + combining
/// characters.  For example, Ä becomes A + umlaut.
/// <li><code>3</code> is Unicode normalization form KC, compatibility
/// composition.  Transforms each base character + combining characters into the
/// precomposed equivalent, and transforms compatibility characters into their
/// equivalents.  For example, A + umlaut + ligature for "fi" becomes Ä + f + i.
/// <li><code>4</code> is Unicode normalization form KD, compatibility
/// decomposition.  Transforms each precomposed character into base character +
/// combining characters, and transforms compatibility characters to their
/// equivalents.  For example, Ä + ligature for "fi" becomes A + umlaut + f + i.
/// </ul>
///
/// If successful, true or false is returned.
///
/// If unsuccessful, false and an error code are returned.
static int isnormalized(lua_State* state)
{
    bool isnum;
    int form = checkinteger(state, 1, &isnum);
    const char* text = checkstring(state, 2);
    if (!isnum || !text)
        return 0;

    if (form < 1 || form > 4)
    {
        luaL_argerror(state, 1, "must be an integer 1 to 4");
        return 0;
    }

    NORM_FORM norm;
    switch (form)
    {
    case 1: norm = NormalizationC; break;
    case 2: norm = NormalizationD; break;
    case 3: norm = NormalizationKC; break;
    case 4: norm = NormalizationKD; break;
    default: assert(false); return 0;
    }

    wstr_moveable in(text);
    BOOL ret = s_normaliz.IsNormalizedString(norm, in.c_str(), in.length());
    DWORD err = GetLastError();

    lua_pushboolean(state, !!ret);
    if (err)
        lua_pushinteger(state, err);
    return err ? 2 : 1;
}

//------------------------------------------------------------------------------
/// -name:  unicode.iter
/// -ver:   1.3.26
/// -arg:   text:string
/// -ret:   iterator
/// This returns an iterator which steps through <span class="arg">text</span>
/// one Unicode codepoint at a time.  Each call to the iterator returns the
/// string for the next codepoint, the numeric value of the codepoint, and a
/// boolean indicating whether the codepoint is a combining mark.
/// -show:  -- UTF8 sample string:
/// -show:  -- Index by codepoint:   1       2       3           4       5
/// -show:  -- Unicode character:    à       é       ᴆ           õ       û
/// -show:  local text            = "\xc3\xa0\xc3\xa9\xe1\xb4\x86\xc3\xb5\xc3\xbb"
/// -show:  -- Index by byte:        1   2   3   4   5   6   7   8   9   10  11
/// -show:
/// -show:  for str, value, combining in unicode.iter(text) do
/// -show:      -- Note that the default lua print() function is not fully aware
/// -show:      -- of Unicode, so clink.print() is needed to print Unicode text.
/// -show:      local bytes = ""
/// -show:      for i = 1, #str do
/// -show:          bytes = bytes .. string.format("\\x%02x", str:byte(i, i))
/// -show:      end
/// -show:      clink.print(str, value, combining, bytes)
/// -show:  end
/// -show:
/// -show:  -- The above prints the following:
/// -show:  --      à       224     false   \xc3\xa0
/// -show:  --      é       233     false   \xc3\xa9
/// -show:  --      ᴆ       7430    false   \xe1\xb4\x86
/// -show:  --      õ       245     false   \xc3\xb5
/// -show:  --      û       373     false   \xc3\xbb
static int iter_aux (lua_State* state)
{
    const char* text = lua_tolstring(state, lua_upvalueindex(1), nullptr);
    const int pos = int(lua_tointeger(state, lua_upvalueindex(2)));
    const char* s = text + pos;

    str_iter iter(s);
    const int c = iter.next();
    if (!c)
        return 0;

    const char* e = iter.get_pointer();

    lua_pushinteger(state, int(e - text));
    lua_replace(state, lua_upvalueindex(2));

    lua_pushlstring(state, s, size_t(e - s));
    lua_pushinteger(state, c);
    lua_pushboolean(state, is_combining_mark(c));
    return 3;
}
static int iter(lua_State* state)
{
    const char* s = checkstring(state, 1);
    if (!s)
        return 0;

    lua_settop(state, 1);                   // Reuse the pushed string.
    lua_pushinteger(state, 0);              // Push a position for the next iteration.
    lua_pushcclosure(state, iter_aux, 2);
    return 1;
}

//------------------------------------------------------------------------------
void unicode_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "normalize",      &normalize },
        { "isnormalized",   &isnormalized },
        { "iter",           &iter },    // TODO: return an iterator that returns string for one codepoint, value of the codepoint, and whether it is a combining mark.
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "unicode");
}
