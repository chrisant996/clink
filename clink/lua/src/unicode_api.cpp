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
/* translate a relative string position: negative means back from end */
static size_t posrelat (ptrdiff_t pos, size_t len) {
    if (pos >= 0) return (size_t)pos;
    else if (0u - (size_t)pos > len) return 0;
    else return len - ((size_t)-pos) + 1;
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
/// -name:  unicode.sub
/// -ver:   1.3.26
/// -arg:   text:string
/// -arg:   start:integer
/// -arg:   end:integer
/// -ret:   string
/// This is like
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-string.sub"><code>string.sub</code></a>,
/// but <span class="arg">start</span> and <span class="arg">end</span> refer to
/// the number of Unicode codepoints, instead of the number of bytes.  This is
/// useful for performing substring manipulation on Unicode strings, without
/// severing Unicode characters.
/// -show:  -- UTF8 sample string:
/// -show:  -- Index by codepoint:   1       2       3           4       5
/// -show:  -- Unicode character:    à       é       ᴆ           õ       û
/// -show:  local text            = "\xc3\xa0\xc3\xa9\xe1\xb4\x86\xc3\xb5\xc3\xbb"
/// -show:  -- Index by byte:        1   2   3   4   5   6   7   8   9   10  11
/// -show:
/// -show:  -- Get substring specified by Unicode codepoint index, rather than by byte index:
/// -show:  -- Start at the 2nd Unicode codepoint, which in this case is at the 3rd byte.
/// -show:  -- End after the 3rd Unicode codepoint, which in this case is at the 8th byte.
/// -show:  local sub = unicode.sub(text, 2, 3) -- Receives "\xc3\xa9\xe1\xb4\x86", which is "éᴆ".
/// -show:
/// -show:  clink.print(sub) -- Prints "éᴆ".
/// -show:
/// -show:  -- Note that the default lua print() function is not fully aware of
/// -show:  -- Unicode.  It converts the Unicode string to the current locale
/// -show:  -- encoding, and won't print the intended string.
/// -show:  print(sub)       -- Depending on the current locale, this may print "é?" or something else.
static int usub(lua_State* state)
{
    bool isnum;
    const char* s = checkstring(state, 1);
    int i = checkinteger(state, 2, &isnum);
    int j = optinteger(state, 3, -1);
    if (!s || !isnum)
        return 0;

    size_t start;
    size_t end;

    if (i < 0 || j < 0)
    {
        size_t l = 0;
        str_iter iter(s);
        while (iter.next())
            l++;

        start = posrelat(i, l);
        end = posrelat(j, l);

        if (end > l)
            end = l;
    }
    else
    {
        start = size_t(i);
        end = size_t(j);
    }

    if (start < 1)
        start = 1;
    start--;

    if (start <= end)
    {
        str_iter iter(s);

        end -= start;
        while (start--)
            iter.next();
        const char* p = iter.get_pointer();

        while (end--)
            iter.next();
        const char* q = iter.get_pointer();

        lua_pushlstring(state, p, size_t(q - p));
    }
    else
    {
        lua_pushliteral(state, "");
    }

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
        { "sub",            &usub },
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
