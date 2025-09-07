// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "sessionstream.h"
#include "lua_state.h"

#include <core/str_unordered_set.h>
#include <core/debugheap.h>

#include <assert.h>

//------------------------------------------------------------------------------
static str_unordered_map<std::shared_ptr<session_stream>>* s_streams = nullptr;
constexpr stream_position_t MAX_SESSION_STREAM_SIZE = 4 * 1024 * 1024;  // limit session streams to 4MB

//------------------------------------------------------------------------------
void discard_all_session_streams()
{
    auto* streams = s_streams;
    s_streams = nullptr; // Before delete, otherwise ~session_stream uses it and crashes.
    delete streams;
}

//------------------------------------------------------------------------------
session_stream::session_stream(const char* name)
: m_name(name)
{
}

//------------------------------------------------------------------------------
session_stream::~session_stream()
{
    clear();

    if (s_streams)
    {
        auto it = s_streams->find(m_name.c_str());
        if (it != s_streams->end())
            s_streams->erase(it);
    }
}

//------------------------------------------------------------------------------
stream_position_t session_stream::write(stream_position_t& offset, const char* buffer, stream_position_t _count)
{
    stream_position_t actual_count = _count;

    {
        const stream_position_t count_plus_nul = _count + 1;
        if (offset + count_plus_nul > m_capacity)
        {
            stream_position_t capacity = max<stream_position_t>(LUAL_BUFFERSIZE, m_capacity * 3 / 2);
            if (capacity < offset + count_plus_nul)
                capacity = (offset + count_plus_nul) * 3 / 2;
            capacity = min<>(capacity, MAX_SESSION_STREAM_SIZE);
            uint8* data = (uint8*)realloc(m_data, capacity);
            if (!data)
                return 0;
#ifdef USE_MEMORY_TRACKING
            dbgsetignore(data, true);
            dbgsetlabel(data, "session_stream::m_data", false);
#endif
            m_data = data;
            m_capacity = capacity;
        }
    }

    const stream_position_t usable_capacity = m_capacity - 1;  // -1 for NUL terminator.
    if (offset >= usable_capacity)
    {
        actual_count = 0;
    }
    else
    {
        actual_count = min<>(actual_count, usable_capacity - offset);
        memcpy(m_data + offset, buffer, actual_count);
        offset += actual_count;
        assert(offset < m_capacity);
        if (m_size < offset)
        {
            m_size = offset;
            m_data[m_size] = 0;  // NUL terminate to simplify scan_number() and for general safety.
        }
    }

    assert(m_size < m_capacity);
    return actual_count;
}

//------------------------------------------------------------------------------
stream_position_t session_stream::read(stream_position_t& offset, char* buffer, stream_position_t max, bool text_mode)
{
    stream_position_t num = 0;
    if (max > 0)
    {
        if (text_mode)
        {
            stream_position_t remaining = max;
            while (remaining && offset < m_size)
            {
                const char c = m_data[offset++];
                *buffer = c;
                if (c == '\r' && offset < m_size && m_data[offset] == '\n')
                {
                    ++offset;
                    *buffer = '\n';
                }
                ++num;
                ++buffer;
                --remaining;
            }
        }
        else
        {
            if (m_size > offset)
            {
                num = min<>(max, m_size - offset);
                memcpy(buffer, m_data + offset, num);
                offset += num;
            }
        }
        assert(num <= max);
    }
    return num;
}

//------------------------------------------------------------------------------
stream_position_t session_stream::gets(stream_position_t& offset, char* buffer, stream_position_t max, bool text_mode)
{
    stream_position_t num = 0;
    if (max > 0)
    {
        --max;
        while (m_size > offset && num < max)
        {
            const uint8 c = m_data[offset++];
            buffer[num++] = c;
            if (c == '\n')
            {
                if (text_mode && num >= 2 && buffer[num - 2] == '\r')
                {
                    --num;
                    buffer[num - 1] = c;
                }
                break;
            }
        }
        assert(num <= max);
        buffer[num] = 0;
    }
    return num;
}

//------------------------------------------------------------------------------
int32 session_stream::scan_number(stream_position_t& offset, double* d)
{
    if (m_size <= offset)
        return 0;
    // To help simplify parsing in scan_number(), session_stream makes sure
    // that m_data is always NUL terminated.
    int32 scanned = 0;
    const char* begin = reinterpret_cast<const char*>(m_data + offset);
    const int32 assigned = sscanf(begin, LUA_NUMBER_SCAN "%n", d, &scanned);
    offset += scanned;
    return (assigned == 1) ? 1 : 0;
}

//------------------------------------------------------------------------------
bool session_stream::truncate(stream_position_t offset)
{
    if (m_size > offset)
        m_size = offset;
    return true;
}

//------------------------------------------------------------------------------
void session_stream::clear()
{
    free(m_data);
    m_data = nullptr;
    m_size = 0;
    m_capacity = 0;
}



//------------------------------------------------------------------------------
luaL_SessionStream* tostream(lua_State* state, bool require)
{
    luaL_SessionStream* ss;
    if (require)
        ss = (luaL_SessionStream*)luaL_checkudata(state, LUA_SELF, LUA_SESSIONSTREAM);
    else
        ss = (luaL_SessionStream*)luaL_testudata(state, LUA_SELF, LUA_SESSIONSTREAM);
    if (ss && ss->is_closed())
        luaL_error(state, "attempt to use a closed sessionstream");
    return ss;
}

//------------------------------------------------------------------------------
luaL_SessionStream* luaL_SessionStream::make_new(lua_State* state, const char* name, OpenFlags flags, bool clear)
{
    if (!s_streams)
        s_streams = new str_unordered_map<std::shared_ptr<session_stream>>;

    // Look for an existing named stream.
    std::shared_ptr<session_stream> stream;
    auto it = s_streams->find(name);
    if (it != s_streams->end())
        stream = it->second;

    if (stream)
    {
        // Maybe fail if exists.
        if ((flags & OpenFlags::ONLYCREATE) == OpenFlags::ONLYCREATE)
        {
            errno = EEXIST;
            return nullptr;
        }
    }
    else
    {
        // Maybe create a new named stream.
        if ((flags & OpenFlags::CREATE) != OpenFlags::CREATE)
        {
            errno = ENOENT;
            return nullptr;
        }

        stream = std::make_shared<session_stream>(name);
        assert(s_streams->find(name) == s_streams->end());
        s_streams->emplace(stream->name(), stream);
    }

#ifdef DEBUG
    int32 oldtop = lua_gettop(state);
#endif

    luaL_SessionStream* ss = (luaL_SessionStream*)lua_newuserdata(state, sizeof(luaL_SessionStream));
    new (ss) luaL_SessionStream(stream, flags);

    if (clear)
    {
        ss->clear();
        ss->m_offset = 0;
    }

    static const luaL_Reg sslib[] =
    {
        {"close", close},
        {"flush", flush},
        {"lines", lines},
        {"read", read},
        {"seek", seek},
        {"setvbuf", setvbuf},
        {"write", write},
        {"__gc", __gc},
        {"__tostring", __tostring},
        {nullptr, nullptr}
    };

    if (luaL_newmetatable(state, LUA_SESSIONSTREAM))
    {
        lua_pushvalue(state, -1);           // push metatable
        lua_setfield(state, -2, "__index"); // metatable.__index = metatable
        luaL_setfuncs(state, sslib, 0);     // add methods to new metatable
    }
    lua_setmetatable(state, -2);

#ifdef DEBUG
    int32 newtop = lua_gettop(state);
    assert(oldtop - newtop == -1);
    luaL_SessionStream* test = (luaL_SessionStream*)luaL_checkudata(state, -1, LUA_SESSIONSTREAM);
    assert(test == ss);
#endif

    return ss;
}

//------------------------------------------------------------------------------
luaL_SessionStream::luaL_SessionStream(std::shared_ptr<session_stream> stream, OpenFlags flags)
: m_flags(flags)
, m_text_mode((flags & OpenFlags::BINARY) != OpenFlags::BINARY)
, m_name(stream->name())
, m_stream(stream)
{
}

//------------------------------------------------------------------------------
void luaL_SessionStream::clear()
{
    m_stream->clear();
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::fileresult(lua_State* state, int32 stat, const luaL_SessionStream* ss)
{
    return luaL_fileresult(state, stat, ss ? ss->m_name.c_str() : "nil");
}

//------------------------------------------------------------------------------
bool luaL_SessionStream::eof() const
{
    return !m_stream || m_offset >= size();
}

//------------------------------------------------------------------------------
bool luaL_SessionStream::is_closed() const
{
    return !m_stream;
}

//------------------------------------------------------------------------------
bool luaL_SessionStream::is_writable() const
{
    return (m_flags & OpenFlags::WRITE) == OpenFlags::WRITE;
}

//------------------------------------------------------------------------------
bool luaL_SessionStream::is_readable() const
{
    return (m_flags & OpenFlags::READ) == OpenFlags::READ;
}

//------------------------------------------------------------------------------
stream_position_t luaL_SessionStream::size() const
{
    if (!m_stream)
        return 0;
    return m_stream->size();
}

//------------------------------------------------------------------------------
stream_position_t luaL_SessionStream::write(const char* buffer, stream_position_t count)
{
    if (!m_stream || !is_writable())
        return 0;
    if (!m_text_mode)
        return m_stream->write(m_offset, buffer, count);
    stream_position_t total = 0;
    stream_position_t len = 0;
    for (const char* p = buffer; count; ++p, ++len, --count)
    {
        if (*p == '\n' || len == 4096)
        {
            if (len)
                total += m_stream->write(m_offset, buffer, len);
            if (*p == '\n')
                m_stream->write(m_offset, "\r", 1);
            buffer = p;
            len = 0;
        }
    }
    if (len)
        total += m_stream->write(m_offset, buffer, len);
    return total;
}

//------------------------------------------------------------------------------
stream_position_t luaL_SessionStream::read(char* buffer, stream_position_t max)
{
    if (!m_stream || !is_readable())
        return 0;
    return m_stream->read(m_offset, buffer, max, m_text_mode);
}

//------------------------------------------------------------------------------
stream_position_t luaL_SessionStream::gets(char* buffer, stream_position_t max)
{
    if (!m_stream || !is_readable())
        return 0;
    return m_stream->gets(m_offset, buffer, max, m_text_mode);
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::scan_number(double* d)
{
    if (!m_stream || !is_readable())
    {
        *d = 0;
        return 0;
    }
    return m_stream->scan_number(m_offset, d);  // format string is LUA_NUMBER_SCAN.
}

//------------------------------------------------------------------------------
bool luaL_SessionStream::truncate()
{
    if (!m_stream)
        return false;
    if (!is_writable())
        return true;
    return m_stream->truncate(m_offset);
}

//------------------------------------------------------------------------------
bool luaL_SessionStream::close()
{
    if (!m_stream)
        return false;
    m_stream.reset();
    return true;
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::close(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    const bool ok = ss->close();
    return fileresult(state, ok, ss);
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::flush(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    // Does nothing.
    return fileresult(state, true, ss);
}

//------------------------------------------------------------------------------
static int32 ss_read(lua_State* state, luaL_SessionStream* ss, int32 first);
static int32 ss_aux_close(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    const bool ok = ss->close();
    return luaL_fileresult(state, ok, NULL);
}
static int32 ss_readline_closure(lua_State* state)
{
    luaL_SessionStream* ss = (luaL_SessionStream*)lua_touserdata(state, lua_upvalueindex(1));
    int32 n = (int32)lua_tointeger(state, lua_upvalueindex(2));
    if (ss->is_closed())  // file is already closed?
        return luaL_error(state, "sessionstream is already closed");
    lua_settop(state, 1);
    for (int32 i = 1; i <= n; i++)  // push arguments to 'ss_read'
        lua_pushvalue(state, lua_upvalueindex(2 + i));
    n = ss_read(state, ss, 2);  // 'n' is number of results
    lua_assert(n > 0);  // should return at least a nil
    if (!lua_isnil(state, -n))  // read at least one value?
        return n;  // return them
    else
    {
        // first result is nil: EOF or error
        if (n > 1)
        {
            // is there error information?
            // 2nd result is error message
            return luaL_error(state, "%s", lua_tostring(state, -n + 1));
        }
        return 0;
    }
}
int32 luaL_SessionStream::lines(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    int32 i;
    int32 n = lua_gettop(state) - 1;  // number of arguments to read
    // ensure that arguments will fit here and into 'io_readline' stack
    luaL_argcheck(state, n <= LUA_MINSTACK - 3, LUA_MINSTACK - 3, "too many options");
    lua_pushvalue(state, 1);  // sessionstream handle
    lua_pushinteger(state, n);  // number of arguments to read
    for (i = 1; i <= n; i++) lua_pushvalue(state, i + 1);  // copy arguments
    lua_pushcclosure(state, ss_readline_closure, 2 + n);
    return 1;
}

//------------------------------------------------------------------------------
static int32 ss_read_number(lua_State* state, luaL_SessionStream* ss)
{
    lua_Number d;
    if (ss->scan_number(&d) == 1)
    {
        lua_pushnumber(state, d);
        return 1;
    }
    else
    {
        lua_pushnil(state);  // "result" to be removed
        return 0;  // read fails
    }
}
static int32 ss_test_eof(lua_State* state, luaL_SessionStream* ss)
{
    lua_pushlstring(state, nullptr, 0);
    return ss->eof();
}
static int32 ss_read_line(lua_State* state, luaL_SessionStream* ss, bool chop)
{
    luaL_Buffer b;
    luaL_buffinit(state, &b);
    for (;;)
    {
        size_t l;
        char* p = luaL_prepbuffer(&b);
        if (!ss->gets(p, LUAL_BUFFERSIZE))
        {
            // eof?
            luaL_pushresult(&b);  // close buffer
            return (lua_rawlen(state, -1) > 0);  // check whether read something
        }
        l = strlen(p);
        if (l == 0 || p[l-1] != '\n')
            luaL_addsize(&b, l);
        else
        {
            if (chop)  // chop 'eol' if needed
                --l;
            luaL_addsize(&b, l);
            luaL_pushresult(&b);  // close buffer
            return 1;  // read at least an `eol'
        }
    }
}
static void ss_read_all(lua_State* state, luaL_SessionStream* ss)
{
    stream_position_t rlen = LUAL_BUFFERSIZE;  // how much to read in each cycle
    luaL_Buffer b;
    luaL_buffinit(state, &b);
    for (;;)
    {
        char* p = luaL_prepbuffsize(&b, rlen);
        stream_position_t nr = ss->read(p, rlen);
        luaL_addsize(&b, nr);
        if (nr < rlen) break;  // eof?
        else rlen *= 2;  // double buffer size at each iteration
        rlen = min<stream_position_t>(rlen, MAX_SESSION_STREAM_SIZE);
    }
    luaL_pushresult(&b);  // close buffer
}
static int32 ss_read_chars(lua_State* state, luaL_SessionStream* ss, stream_position_t n)
{
    stream_position_t nr;  // number of chars actually read
    char* p;
    luaL_Buffer b;
    luaL_buffinit(state, &b);
    p = luaL_prepbuffsize(&b, n);  // prepare buffer to read whole block
    nr = ss->read(p, n);  // try to read 'n' chars
    luaL_addsize(&b, nr);
    luaL_pushresult(&b);  // close buffer
    return (nr > 0);  // true iff read something
}
static int32 ss_read(lua_State* state, luaL_SessionStream* ss, int32 first)
{
    if (!ss->is_readable())
    {
        errno = EBADF;
        return luaL_fileresult(state, false, nullptr);
    }

    int32 nargs = lua_gettop(state) - 1;
    int32 success;
    int32 n;
    if (nargs == 0)
    {
        // no arguments?
        success = ss_read_line(state, ss, true/*chop*/);
        n = first+1;  // to return 1 result
    }
    else
    {
        // ensure stack space for all results and for auxlib's buffer
        luaL_checkstack(state, nargs+LUA_MINSTACK, "too many arguments");
        success = 1;
        for (n = first; nargs-- && success; n++)
        {
            if (lua_type(state, n) == LUA_TNUMBER)
            {
                size_t l = (size_t)lua_tointeger(state, n);
                stream_position_t limited = stream_position_t(min<size_t>(l, MAX_SESSION_STREAM_SIZE));
                success = (l == 0) ? ss_test_eof(state, ss) : ss_read_chars(state, ss, limited);
            }
            else
            {
                const char* p = lua_tostring(state, n);
                luaL_argcheck(state, p && p[0] == '*', n, "invalid option");
                switch (p[1])
                {
                case 'n':  // number
                    success = ss_read_number(state,ss);
                    break;
                case 'l':  // line
                    success = ss_read_line(state, ss, true/*chop*/);
                    break;
                case 'L':  // line with end-of-line
                    success = ss_read_line(state, ss, false/*chop*/);
                    break;
                case 'a':  // file
                    ss_read_all(state, ss);  // read entire file
                    success = 1; // always success
                    break;
                default:
                    return luaL_argerror(state, n, "invalid format");
                }
            }
        }
    }
    if (!success)
    {
        lua_pop(state, 1);  // remove last result
        lua_pushnil(state);  // push nil instead
    }
    return n - first;
}
int32 luaL_SessionStream::read(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    return ss_read(state, ss, LUA_SELF + 1);
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::seek(lua_State* state)
{
    static const int32 mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    static const char* const modenames[] = {"set", "cur", "end", nullptr};
    luaL_SessionStream* ss = tostream(state);

    int32 op = luaL_checkoption(state, LUA_SELF + 1, "cur", modenames);
    lua_Number p3 = luaL_optnumber(state, LUA_SELF + 2, 0);
    __int64 offset = (__int64)p3;
    luaL_argcheck(state, (lua_Number)offset == p3, LUA_SELF + 2, "not an integer in proper range");

    unsigned __int64 base;
    switch (op)
    {
    default:
    case 0: base = 0; break;
    case 1: base = ss->m_offset; break;
    case 2: base = ss->size(); break;
    }

    stream_position_t limited = stream_position_t(min<unsigned __int64>(base + offset, MAX_SESSION_STREAM_SIZE));

    ss->m_offset = limited;
    lua_pushinteger(state, ss->m_offset);
    return 1;
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::setvbuf(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    // Does nothing.
    return luaL_fileresult(state, true, nullptr);
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::write(lua_State* state)
{
    luaL_SessionStream* ss = tostream(state);
    if (!ss->is_writable())
    {
        errno = EBADF;
        return luaL_fileresult(state, false, nullptr);
    }

    lua_pushvalue(state, 1);  // push stream at the stack top (to be returned)

    int32 arg = LUA_SELF + 1;
    int32 nargs = lua_gettop(state) - arg;
    int32 status = 1;
    for (; status && nargs--; arg++)
    {
        size_t l;
        const char* s = luaL_checklstring(state, arg, &l);
        if (status)
        {
            if (!ss->is_writable())
            {
                errno = EBADF;
                status = false;
                break;
            }
            if ((ss->m_flags & OpenFlags::APPEND) == OpenFlags::APPEND)
                ss->m_offset = ss->size();
            const stream_position_t limited = stream_position_t(min<size_t>(l, MAX_SESSION_STREAM_SIZE));
            const stream_position_t wrote = ss->write(s, limited);
            status = (wrote == l);
        }
    }

    if (status) return 1;  // file handle already on stack top
    else return luaL_fileresult(state, status, nullptr);
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::__gc(lua_State* state)
{
    luaL_SessionStream* ss = (luaL_SessionStream*)luaL_checkudata(state, LUA_SELF, LUA_SESSIONSTREAM);
    if (ss)
        ss->~luaL_SessionStream();
    return 0;
}

//------------------------------------------------------------------------------
int32 luaL_SessionStream::__tostring(lua_State* state)
{
    luaL_SessionStream* ss = (luaL_SessionStream*)luaL_checkudata(state, LUA_SELF, LUA_SESSIONSTREAM);
    if (!ss->m_stream)
        lua_pushliteral(state, "sessionstream (closed)");
    else
        lua_pushfstring(state, "sessionstream (%p) " LUA_QS, ss, ss ? ss->m_name.c_str() : "");
    return 1;
}
