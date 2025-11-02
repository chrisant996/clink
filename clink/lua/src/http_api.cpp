// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_bindable.h"
#include "async_lua_task.h"
#include "yield.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/debugheap.h>
#include <assert.h>

#include <winhttp.h>
#include <wininet.h>
#include <VersionHelpers.h>
#include <mutex>

extern "C" {
#include <lstate.h>
}

//------------------------------------------------------------------------------
static class delay_load_winhttp
{
public:
                        delay_load_winhttp();
    bool                init();
    HINTERNET           WinHttpOpen(LPCWSTR pszAgentW, DWORD dwAccessType, LPCWSTR pszProxyW, LPCWSTR pszProxyBypassW, DWORD dwFlags);
    BOOL                WinHttpCloseHandle(HINTERNET hInternet);
    BOOL                WinHttpCrackUrl(LPCWSTR pwszUrl, DWORD dwUrlLength, DWORD dwFlags, LPURL_COMPONENTSW lpUrlComponents);
    HINTERNET           WinHttpConnect(HINTERNET hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort, DWORD dwReserved);
    HINTERNET           WinHttpOpenRequest(HINTERNET hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName, LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR FAR * ppwszAcceptTypes, DWORD dwFlags);
    BOOL                WinHttpAddRequestHeaders(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers);
    BOOL                WinHttpSendRequest(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength, DWORD_PTR dwContext);
    BOOL                WinHttpReceiveResponse(HINTERNET hRequest, LPVOID lpReserved);
    BOOL                WinHttpQueryDataAvailable(HINTERNET hRequest, LPDWORD lpdwNumberOfBytesAvailable);
    BOOL                WinHttpReadData(HINTERNET hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[10];
        struct {
            HINTERNET (WINAPI* WinHttpOpen)(LPCWSTR pszAgentW, DWORD dwAccessType, LPCWSTR pszProxyW, LPCWSTR pszProxyBypassW, DWORD dwFlags);
            BOOL (WINAPI* WinHttpCloseHandle)(HINTERNET hInternet);
            BOOL (WINAPI* WinHttpCrackUrl)(LPCWSTR pwszUrl, DWORD dwUrlLength, DWORD dwFlags, LPURL_COMPONENTSW lpUrlComponents);
            HINTERNET (WINAPI* WinHttpConnect)(HINTERNET hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort, DWORD dwReserved);
            HINTERNET (WINAPI* WinHttpOpenRequest)(HINTERNET hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName, LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR FAR * ppwszAcceptTypes, DWORD dwFlags);
            BOOL (WINAPI* WinHttpAddRequestHeaders)(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers);
            BOOL (WINAPI* WinHttpSendRequest)(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength, DWORD_PTR dwContext);
            BOOL (WINAPI* WinHttpReceiveResponse)(HINTERNET hRequest, LPVOID lpReserved);
            BOOL (WINAPI* WinHttpQueryDataAvailable)(HINTERNET hRequest, LPDWORD lpdwNumberOfBytesAvailable);
            BOOL (WINAPI* WinHttpReadData)(HINTERNET hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead);
        };
    } m_procs;
} s_winhttp;

//------------------------------------------------------------------------------
delay_load_winhttp::delay_load_winhttp()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

//------------------------------------------------------------------------------
bool delay_load_winhttp::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        HMODULE hlib = LoadLibrary("winhttp.dll");
        if (hlib)
        {
            size_t c = 0;
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpOpen");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpCloseHandle");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpCrackUrl");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpConnect");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpOpenRequest");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpAddRequestHeaders");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpSendRequest");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpReceiveResponse");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpQueryDataAvailable");
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpReadData");
            assert(_countof(m_procs.proc) == c);
        }

        m_ok = true;
        static_assert(sizeof(m_procs.proc) == sizeof(m_procs), "proc[] dimension is too small");
        for (auto const& proc : m_procs.proc)
        {
            if (!proc)
            {
                m_ok = false;
                break;
            }
        }
    }

assert(m_ok);
    return m_ok;
}

//------------------------------------------------------------------------------
HINTERNET delay_load_winhttp::WinHttpOpen(LPCWSTR pszAgentW, DWORD dwAccessType, LPCWSTR pszProxyW, LPCWSTR pszProxyBypassW, DWORD dwFlags)
{
    if (!init())
        return NULL;
    return m_procs.WinHttpOpen(pszAgentW, dwAccessType, pszProxyW, pszProxyBypassW, dwFlags);
}

BOOL delay_load_winhttp::WinHttpCloseHandle(HINTERNET hInternet)
{
    if (!init())
        return false;
    return m_procs.WinHttpCloseHandle(hInternet);
}

BOOL delay_load_winhttp::WinHttpCrackUrl(LPCWSTR pwszUrl, DWORD dwUrlLength, DWORD dwFlags, LPURL_COMPONENTSW lpUrlComponents)
{
    if (!init())
        return false;
    return m_procs.WinHttpCrackUrl(pwszUrl, dwUrlLength, dwFlags, lpUrlComponents);
}

HINTERNET delay_load_winhttp::WinHttpConnect(HINTERNET hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort, DWORD dwReserved)
{
    if (!init())
        return NULL;
    return m_procs.WinHttpConnect(hSession, pswzServerName, nServerPort, dwReserved);
}

HINTERNET delay_load_winhttp::WinHttpOpenRequest(HINTERNET hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName, LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR FAR * ppwszAcceptTypes, DWORD dwFlags)
{
    if (!init())
        return NULL;
    return m_procs.WinHttpOpenRequest(hConnect, pwszVerb, pwszObjectName, pwszVersion, pwszReferrer, ppwszAcceptTypes, dwFlags);
}

BOOL delay_load_winhttp::WinHttpAddRequestHeaders(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers)
{
    if (!init())
        return false;
    return m_procs.WinHttpAddRequestHeaders(hRequest, lpszHeaders, dwHeadersLength, dwModifiers);
}

BOOL delay_load_winhttp::WinHttpSendRequest(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength, DWORD_PTR dwContext)
{
    if (!init())
        return false;
    return m_procs.WinHttpSendRequest(hRequest, lpszHeaders, dwHeadersLength, lpOptional, dwOptionalLength, dwTotalLength, dwContext);
}

BOOL delay_load_winhttp::WinHttpReceiveResponse(HINTERNET hRequest, LPVOID lpReserved)
{
    if (!init())
        return false;
    return m_procs.WinHttpReceiveResponse(hRequest, lpReserved);
}

BOOL delay_load_winhttp::WinHttpQueryDataAvailable(HINTERNET hRequest, LPDWORD lpdwNumberOfBytesAvailable)
{
    if (!init())
        return false;
    return m_procs.WinHttpQueryDataAvailable(hRequest, lpdwNumberOfBytesAvailable);
}

BOOL delay_load_winhttp::WinHttpReadData(HINTERNET hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead)
{
    if (!init())
        return false;
    return m_procs.WinHttpReadData(hRequest, lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead);
}



#if 0
//------------------------------------------------------------------------------
extern "C" void __cdecl __acrt_errno_map_os_error(unsigned long const oserrno);
static void map_errno() { __acrt_errno_map_os_error(GetLastError()); }
static void map_errno(unsigned long const oserrno) { __acrt_errno_map_os_error(oserrno); }

//------------------------------------------------------------------------------
static int32 lua_osboolresult(lua_State *state, bool stat, const char *tag=nullptr)
{
    int32 en = errno;  /* calls to Lua API may change this value */

    lua_pushboolean(state, stat);

    if (stat)
        return 1;

    if (tag)
        lua_pushfstring(state, "%s: %s", tag, strerror(en));
    else
        lua_pushfstring(state, "%s", strerror(en));
    lua_pushinteger(state, en);
    return 3;
}

//------------------------------------------------------------------------------
static int32 lua_osstringresult(lua_State *state, const char* result, bool stat, const char *tag=nullptr)
{
    int32 en = errno;  /* calls to Lua API may change this value */

    if (stat)
    {
        lua_pushstring(state, result);
        return 1;
    }

    lua_pushnil(state);
    if (tag)
        lua_pushfstring(state, "%s: %s", tag, strerror(en));
    else
        lua_pushfstring(state, "%s", strerror(en));
    lua_pushinteger(state, en);
    return 3;
}
#endif



//------------------------------------------------------------------------------
class httpget_async_lua_task : public async_lua_task
{
public:
    httpget_async_lua_task(const char* key, const char* src, async_yield_lua* asyncyield, const char* url, const char* user_agent, bool no_cache)
    : async_lua_task(key, src)
    , m_url(url)
    , m_user_agent(user_agent)
    , m_nocache(no_cache)
    , m_ismain(!asyncyield)
    {
        set_asyncyield(asyncyield);
    }

    ~httpget_async_lua_task()
    {
        free(m_result_buffer);
    }

    bool result(char*& buffer, size_t& length)
    {
        if (!is_complete())
            return false;
        buffer = m_result_buffer;
        length = m_result_size;
        return true;
    }

    bool wait(uint32 timeout)
    {
        assert(m_ismain);
        const DWORD waited = WaitForSingleObject(get_wait_handle(), timeout);
        return waited == WAIT_OBJECT_0;
    }

protected:
    void do_work() override;

private:
    const str_moveable m_url;
    const str_moveable m_user_agent;
    const bool m_nocache;
    const bool m_ismain;
    std::recursive_mutex m_mutex;
    char* m_result_buffer = nullptr;
    size_t m_result_size = 0;
};

//------------------------------------------------------------------------------
void httpget_async_lua_task::do_work()
{
// TODO:  What if lua_state is recycled while this task thread is running?
// TODO:  track error status and return error info.

    assert(!m_result_buffer);
    assert(!m_result_size);

    wstr_moveable host_buffer;
    wstr_moveable url_path;
    host_buffer.reserve(INTERNET_MAX_URL_LENGTH);
    url_path.reserve(INTERNET_MAX_URL_LENGTH);

    URL_COMPONENTSW comp = { sizeof(comp) };
    comp.lpszHostName = host_buffer.data();
    comp.lpszUrlPath = url_path.data();
    comp.dwHostNameLength = host_buffer.size();
    comp.dwUrlPathLength = url_path.size();

    wstr_moveable wurl(m_url.c_str());
    if (!s_winhttp.WinHttpCrackUrl(wurl.c_str(), wurl.length(), ICU_DECODE, &comp))
    {
final_ret:
        wake_asyncyield();
        return;
    }

    wstr_moveable wua(m_user_agent.c_str());
    const WCHAR* wuser_agent = wua.empty() ? nullptr : wua.c_str();
    const DWORD access_type = IsWindows8OrGreater() ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    HINTERNET hSession = s_winhttp.WinHttpOpen(wuser_agent, access_type, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        goto final_ret;

    HINTERNET hConnect = s_winhttp.WinHttpConnect(hSession, comp.lpszHostName, comp.nPort, 0);
    if (!hConnect)
    {
close_session:
        s_winhttp.WinHttpCloseHandle(hSession);
        goto final_ret;
    }

    DWORD flags = WINHTTP_FLAG_SECURE|WINHTTP_FLAG_ESCAPE_PERCENT;
    if (m_nocache)
        flags |= WINHTTP_FLAG_REFRESH;

    HINTERNET hRequest = s_winhttp.WinHttpOpenRequest(hConnect, L"GET", comp.lpszUrlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
close_connect:
        s_winhttp.WinHttpCloseHandle(hConnect);
        goto close_session;
    }

    BOOL bResults = s_winhttp.WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults)
    {
close_request:
        s_winhttp.WinHttpCloseHandle(hRequest);
        goto close_connect;
    }

    bResults = s_winhttp.WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults)
        goto close_request;

    constexpr uint32 c_block_size = 4096;
    std::vector<char*> blocks;
    char* block = nullptr;
    DWORD available = 0;

    while (!is_canceled())
    {
        if (!available)
        {
            block = static_cast<char*>(malloc(c_block_size));
            available = c_block_size;
            blocks.emplace_back(block);
        }

        DWORD dwDownloaded = 0;
        if (!s_winhttp.WinHttpReadData(hRequest, block, available, &dwDownloaded))
            goto close_request;

        // Finished reading all data.
        if (!dwDownloaded)
        {
            m_result_buffer = static_cast<char*>(malloc(m_result_size));
            if (!m_result_buffer)
                goto close_request;

            char* write = m_result_buffer;
            size_t remaining = m_result_size;
            for (const auto& block : blocks)
            {
                const size_t to_copy = min<size_t>(c_block_size, remaining);
                memcpy(write, block, to_copy);
                write += to_copy;
                remaining -= to_copy;
            }
// TODO:  mark successful.
            goto close_request;
        }

        assert(dwDownloaded <= c_block_size);
        assert(dwDownloaded <= available);
        available -= dwDownloaded;
        m_result_size += dwDownloaded;
    }
}

//------------------------------------------------------------------------------
class httpget_lua
    : public lua_bindable<httpget_lua>
{
public:
                        httpget_lua(const std::shared_ptr<httpget_async_lua_task>& task) : m_task(task) {}
                        ~httpget_lua() {}

    int32               result(lua_State* state);

private:
    std::shared_ptr<httpget_async_lua_task> m_task;

    friend class lua_bindable<httpget_lua>;
    static const char* const c_name;
    static const httpget_lua::method c_methods[];
};

//------------------------------------------------------------------------------
int32 httpget_lua::result(lua_State* state)
{
    if (!m_task)
        return 0;

    char* data = nullptr;
    size_t length = 0;
    if (!m_task->result(data, length) || !data)
        return 0;

    lua_pushlstring(state, data, length);
    return 1;
}

//------------------------------------------------------------------------------
const char* const httpget_lua::c_name = "httpget_lua";
const httpget_lua::method httpget_lua::c_methods[] = {
    { "result",         &result },
    {}
};



//------------------------------------------------------------------------------
inline bool is_main_coroutine(lua_State* state) { return G(state)->mainthread == state; }

//------------------------------------------------------------------------------
static int32 http_get_internal(lua_State* state)
{
    int32 iarg = 1;
    const bool ismain = is_main_coroutine(state);
    const char* const url = checkstring(state, iarg++);
    const char* const user_agent = optstring(state, iarg++, nullptr);
    const bool no_cache = lua_toboolean(state, iarg++);
    if (!url || !*url)
        return 0;

    static uint32 s_counter = 0;
    str_moveable key;
    key.format("httpget||%08x", ++s_counter);

    str<> src;
    get_lua_srcinfo(state, src);

    dbg_ignore_scope(snapshot, "async http get");

    // Push an asyncyield object.
    async_yield_lua* asyncyield = nullptr;
    if (ismain)
    {
        lua_pushnil(state);
    }
    else
    {
        asyncyield = async_yield_lua::make_new(state, "http.get");
        if (!asyncyield)
            return 0;
    }

    // Push a task object.
    auto task = std::make_shared<httpget_async_lua_task>(key.c_str(), src.c_str(), asyncyield, url, user_agent, no_cache);
    if (!task)
        return 0;
    httpget_lua* request = httpget_lua::make_new(state, task);
    if (!request)
        return 0;
    {
        std::shared_ptr<async_lua_task> add(task); // Because MINGW can't handle it inline.
        add_async_lua_task(add);
    }

    // If this is the main coroutine, wait for completion.
    if (ismain)
    {
        if (!task->wait(INFINITE))
            task->cancel();
    }

    return 2;
}



//------------------------------------------------------------------------------
void http_lua_initialise(lua_state& lua)
{
    static const struct {
        const char* name;
        int32       (*method)(lua_State*);
    } methods[] = {
        { "_get_internal",      &http_get_internal },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, 0, sizeof_array(methods));

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "http");
}
