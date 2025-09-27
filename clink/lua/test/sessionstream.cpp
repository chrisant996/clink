// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h" // (so that VSCode can parse the macros, since it parses the wrong pch.h file)

#include <lua/lua_state.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

extern void discard_all_session_streams();

//------------------------------------------------------------------------------
static bool run_test(lua_state& lua, const char* script)
{
    discard_all_session_streams();

    str<> tmp;
    static const char fmt[] =
    "local chunk, msg = load([=[%s]=], [=[%s]=])\n"
    "if not chunk then error(msg) end\n"
    "local result = chunk()\n"
    "return result\n"
    ;
    tmp.format(fmt, script, SECTIONNAME());

    auto state = lua.get_state();
    save_stack_top ss(state);

    int32 err = luaL_loadbuffer(state, tmp.c_str(), tmp.length(), "run_test");
    if (!err)
        err = lua.pcall(0, LUA_MULTRET);
    if (err)
    {
        if (const char* error = lua_tostring(state, -1))
        {
            puts("");
            puts("error:");
            puts(error ? error : "<no error message>");
        }
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
TEST_CASE("Lua sessionstream")
{
    lua_state lua;

    static const char* main_script =
    "function escape(text)\n"
    "    return text and text:gsub('\\r', '\\\\r'):gsub('\\n', '\\\\n') or nil\n"
    "end\n"
    "\n"
    "function verify_data(expected, actual, tag)\n"
    "    if expected ~= actual then\n"
    "        local msg = string.format(\n"
    "            '\\n\\n'..\n"
    "            'expected: \"%s\"\\n'..\n"
    "            'actual:   \"%s\"', escape(expected), escape(actual))\n"
    "        if tag then\n"
    "            msg = tag..'\\n'..msg\n"
    "        end\n"
    "        error(msg)\n"
    "    end\n"
    "end\n"
    ;

    str<> errmsg;
    REQUIRE(lua.do_string(main_script, -1, &errmsg, "main_script"), [&]() {
        puts(errmsg.length() ? errmsg.c_str() : "<no error message>");
    });

    // r, w
    SECTION("basic")
    {
        static const char script[] =
        "local f, data, r\n"
        "local name = 'basic'\n"
        "\n"
        "f = clink.opensessionstream(name)\n"
        "if f then error('stream should not exist') end\n"
        "\n"
        "local payload = 'my data\\n'\n"
        "f = clink.opensessionstream(name, 'w')\n"
        "r = f:write(payload)\n"
        "if r ~= f then error('expected stream handle to be returned') end\n"
        "f:close()\n"
        "\n"
        "f = clink.opensessionstream(name)\n"
        "data = f:read()\n"
        "verify_data(payload:gsub('%s+$', ''), data, 'initial read')\n"
        "local prev = data\n"
        "f:seek('set', 0)\n"
        "data = f:read()\n"
        "verify_data(prev, data, 'after seek')\n"
        "f:close()\n"
        ;
        REQUIRE(run_test(lua, script));
    }

    // r, rb, wb
    SECTION("binary mode")
    {
        static const char script[] =
        "local f, data\n"
        "local n_name = 'binary with LF'\n"
        "local rn_name = 'binary with CRLF'\n"
        "\n"
        "local n_payload = 'one\\ntwo'\n"
        "f = clink.opensessionstream(n_name, 'wb')\n"
        "f:write(n_payload)\n"
        "f = clink.opensessionstream(n_name, 'r')\n"
        "data = f:read()\n"
        "verify_data('one', data, 'text LF read')\n"
        "f = clink.opensessionstream(n_name, 'rb')\n"
        "data = f:read()\n"
        "verify_data('one', data, 'binary LF read')\n"
        "\n"
        "local rn_payload = 'one\\r\\ntwo'\n"
        "f = clink.opensessionstream(rn_name, 'wb')\n"
        "f:write(rn_payload)\n"
        "f = clink.opensessionstream(rn_name, 'r')\n"
        "data = f:read()\n"
        "verify_data('one', data, 'text CRLF read')\n"
        "f = clink.opensessionstream(rn_name, 'rb')\n"
        "data = f:read()\n"
        "verify_data('one\\r', data, 'binary CRLF read')\n"
        ;
        REQUIRE(run_test(lua, script));
    }

    // w, rb
    SECTION("text mode CRLF")
    {
        static const char script[] =
        "local f, data\n"
        "local name = 'crlf'\n"
        "\n"
        "local payload = 'one\\ntwo'\n"
        "f = clink.opensessionstream(name, 'w')\n"
        "f:write(payload)\n"
        "\n"
        "f = clink.opensessionstream(name, 'rb')\n"
        "data = f:read()\n"
        "verify_data('one\\r', data, 'binary text read')\n"
        "\n"
        "f = clink.opensessionstream(name, 'r')\n"
        "data = f:read()\n"
        "verify_data('one', data, 'text read line')\n"
        "\n"
        "f:seek('set', 0)\n"
        "data = f:read(3)\n"
        "verify_data('one', data, 'text read first 3')\n"
        "data = f:read(3)\n"
        "verify_data('\\ntw', data, 'text read next 3')\n"
        "\n"
        "f:seek('set', 0)\n"
        "data = f:read(2)\n"
        "verify_data('on', data, 'text read first 2')\n"
        "data = f:read(4)\n"
        "verify_data('e\\ntw', data, 'text read next 4')\n"
        "\n"
        "f:seek('set', 0)\n"
        "data = f:read(4)\n"
        "verify_data('one\\n', data, 'text read first 4')\n"
        "data = f:read(2)\n"
        "verify_data('tw', data, 'text read next 2')\n"
        "\n"
        "f:seek('set', 0)\n"
        "data = f:read(6)\n"
        "verify_data('one\\ntw', data, 'text read 6')\n"
        ;
        REQUIRE(run_test(lua, script));
    }

    // r+, w+
    SECTION("plus modes")
    {
        static const char script[] =
        "local f, data\n"
        "local name = 'plus modes'\n"
        "\n"
        "local payload = 'some text'\n"
        "f = clink.opensessionstream(name, 'w')\n"
        "f:write(payload)\n"
        "\n"
        "f = clink.opensessionstream(name, 'r+')\n"
        "data = f:read()\n"
        "verify_data(payload, data, 'r+ initial read')\n"
        "f:seek('set', 4)\n"
        "f:write('_')\n"
        "f:seek('set', 0)\n"
        "data = f:read()\n"
        "verify_data('some_text', data, 'r+ read after write')\n"
        "\n"
        "f = clink.opensessionstream(name, 'w+')\n"
        "data = f:read()\n"
        "verify_data(nil, data, 'w+ initial read')\n"
        "\n"
        "f:write('abc')\n"
        "f = clink.opensessionstream(name, 'r+')\n"
        "data = f:read()\n"
        "verify_data('abc', data, 'r+ read after w+ write')\n"
        ;
        REQUIRE(run_test(lua, script));
    }

    // a, a+
    SECTION("append")
    {
        static const char script[] =
        "local f, msg, data, code\n"
        "\n"
        "f = clink.opensessionstream('append', 'a')\n"
        "f:write('hello')\n"
        "f:seek('set', 0)\n"
        "\n"
        "data, msg, code = f:read()\n"
        "if data then error('read in append mode should raise an error') end\n"
        "if code ~= 9 then error('read in append mode should raise error code 9, but got '..(code or 'nil')) end\n"
        "\n"
        "data = f:seek('end', -1)\n"
        "verify_data('4', tostring(data), 'seek in append mode should seek')\n"
        "f:write('world')\n"
        "f:seek('set', 0)\n"
        "data = f:seek('cur', 3)\n"
        "verify_data('3', tostring(data), 'seek cur plus 3 from beginning')\n"
        "f:write('xx')\n"
        "\n"
        "f = clink.opensessionstream('append', 'a+')\n"
        "data = f:read()\n"
        "verify_data('helloworldxx', data, 'a+ initial read')\n"
        "\n"
        "data = f:seek('set', 0)\n"
        "verify_data('0', tostring(data), 'seek to beginning')\n"
        "f:write('zz')\n"
        "f:seek('set', 0)\n"
        "data = f:read()\n"
        "verify_data('helloworldxxzz', data, 'a+ should always append')\n"
        ;
        REQUIRE(run_test(lua, script));
    }

    // wx, w+x
    SECTION("open 'x'")
    {
        static const char script[] =
        "local f, msg\n"
        "\n"
        "f, msg = clink.opensessionstream('excl', 'wx')\n"
        "if not f then error(msg) end\n"
        "f, msg = clink.opensessionstream('excl+', 'wx')\n"
        "if not f then error(msg) end\n"
        "\n"
        "f, msg = clink.opensessionstream('excl', 'wx')\n"
        "if f then error('expected open \"excl\" to fail, but succeeded') end\n"
        "f, msg = clink.opensessionstream('excl+', 'wx')\n"
        "if f then error('expected open \"excl+\" to fail, but succeeded') end\n"
        ;
        REQUIRE(run_test(lua, script));
    }
}
