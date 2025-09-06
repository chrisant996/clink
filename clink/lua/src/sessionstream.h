// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>

#include <memory>

struct lua_State;

#define LUA_SESSIONSTREAM "clink_session_stream"

//------------------------------------------------------------------------------
typedef uint32 stream_position_t;

//------------------------------------------------------------------------------
class session_stream : public std::enable_shared_from_this<session_stream>
{
public:
    session_stream(const char* name);
    ~session_stream();

    const char* name() const { return m_name.c_str(); }
    stream_position_t size() const { return m_size; }
    stream_position_t write(stream_position_t& offset, const char* buffer, stream_position_t count);
    stream_position_t read(stream_position_t& offset, char* buffer, stream_position_t max, bool text_mode);
    stream_position_t gets(stream_position_t& offset, char* buffer, stream_position_t max, bool text_mode);
    int32 scan_number(stream_position_t& offset, double* d);
    bool truncate(stream_position_t offset);
    void clear();

private:
    const str_moveable m_name;
    uint8* m_data = nullptr;
    stream_position_t m_size = 0;
    stream_position_t m_capacity = 0;
};

//------------------------------------------------------------------------------
struct luaL_SessionStream
{
    enum class OpenFlags
    {
        NONE        = 0x00,
        CREATE      = 0x01,
        WRITE       = 0x02,
        READ        = 0x04,
        APPEND      = 0x08,
        ONLYCREATE  = 0x10,
        BINARY      = 0x20,
    };

    static luaL_SessionStream* make_new(lua_State* state, const char* name, OpenFlags flags, bool clear);

    bool eof() const;
    bool is_closed() const;
    bool is_writable() const;
    bool is_readable() const;
    stream_position_t size() const;
    stream_position_t write(const char* buffer, stream_position_t count);
    stream_position_t read(char* buffer, stream_position_t max);
    stream_position_t gets(char* buffer, stream_position_t max);
    int32 scan_number(double* d);
    bool truncate();
    bool close();

    static int32 close(lua_State* state);

protected:
    luaL_SessionStream(std::shared_ptr<session_stream> stream, OpenFlags flags);
    ~luaL_SessionStream() = default;

    void clear();

    static int32 fileresult(lua_State* state, int32 stat, const luaL_SessionStream* ss);

private:
    static int32 flush(lua_State* state);
    static int32 lines(lua_State* state);
    static int32 read(lua_State* state);
    static int32 seek(lua_State* state);
    static int32 setvbuf(lua_State* state);
    static int32 write(lua_State* state);
    static int32 __gc(lua_State* state);
    static int32 __tostring(lua_State* state);

    const OpenFlags m_flags;
    const bool m_text_mode;
    const str_moveable m_name;
    std::shared_ptr<session_stream> m_stream;
    stream_position_t m_offset = 0;
};

DEFINE_ENUM_FLAG_OPERATORS(luaL_SessionStream::OpenFlags);

//------------------------------------------------------------------------------
luaL_SessionStream* tostream(lua_State* state, bool require=true);
