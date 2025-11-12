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
    BOOL                WinHttpQueryHeaders(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName, LPVOID lpBuffer, LPDWORD lpdwBufferLength, LPDWORD lpdwIndex);
    BOOL                WinHttpQueryDataAvailable(HINTERNET hRequest, LPDWORD lpdwNumberOfBytesAvailable);
    BOOL                WinHttpReadData(HINTERNET hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[11];
        struct {
            HINTERNET (WINAPI* WinHttpOpen)(LPCWSTR pszAgentW, DWORD dwAccessType, LPCWSTR pszProxyW, LPCWSTR pszProxyBypassW, DWORD dwFlags);
            BOOL (WINAPI* WinHttpCloseHandle)(HINTERNET hInternet);
            BOOL (WINAPI* WinHttpCrackUrl)(LPCWSTR pwszUrl, DWORD dwUrlLength, DWORD dwFlags, LPURL_COMPONENTSW lpUrlComponents);
            HINTERNET (WINAPI* WinHttpConnect)(HINTERNET hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort, DWORD dwReserved);
            HINTERNET (WINAPI* WinHttpOpenRequest)(HINTERNET hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName, LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR FAR * ppwszAcceptTypes, DWORD dwFlags);
            BOOL (WINAPI* WinHttpAddRequestHeaders)(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, DWORD dwModifiers);
            BOOL (WINAPI* WinHttpSendRequest)(HINTERNET hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength, LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength, DWORD_PTR dwContext);
            BOOL (WINAPI* WinHttpReceiveResponse)(HINTERNET hRequest, LPVOID lpReserved);
            BOOL (WINAPI* WinHttpQueryHeaders)(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName, LPVOID lpBuffer, LPDWORD lpdwBufferLength, LPDWORD lpdwIndex);
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
            m_procs.proc[c++] = GetProcAddress(hlib, "WinHttpQueryHeaders");
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

BOOL delay_load_winhttp::WinHttpQueryHeaders(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName, LPVOID lpBuffer, LPDWORD lpdwBufferLength, LPDWORD lpdwIndex)
{
    if (!init())
        return false;
    return m_procs.WinHttpQueryHeaders(hRequest, dwInfoLevel, pwszName, lpBuffer, lpdwBufferLength, lpdwIndex);
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



//------------------------------------------------------------------------------
struct response_info
{
    void init(HINTERNET hRequest);

    DWORD win32_error = 0;
    DWORD status_code = 0;
    str_moveable status_text;
    str_moveable raw_headers;
    str_moveable content_type;
    size_t content_length = 0;
    bool completed_read = false;

private:
    bool query(HINTERNET hRequest, DWORD dwInfoLevel, str_base& out);
};

//------------------------------------------------------------------------------
void response_info::init(HINTERNET hRequest)
{
    str<> tmp;

    if (query(hRequest, WINHTTP_QUERY_STATUS_CODE, tmp))
        status_code = atoi(tmp.c_str());

    query(hRequest, WINHTTP_QUERY_STATUS_TEXT, status_text);
    query(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, raw_headers);
    query(hRequest, WINHTTP_QUERY_CONTENT_TYPE, content_type);

    if (query(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, tmp))
        content_length = atoi(tmp.c_str());
}

//------------------------------------------------------------------------------
bool response_info::query(HINTERNET hRequest, DWORD dwInfoLevel, str_base& out)
{
    out.clear();

    DWORD dwSize = 0;
    s_winhttp.WinHttpQueryHeaders(hRequest, dwInfoLevel, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &dwSize, WINHTTP_NO_HEADER_INDEX);
    switch (GetLastError())
    {
    case ERROR_SUCCESS:
        return true;
    case ERROR_INSUFFICIENT_BUFFER:
        break;
    default:
        assert(false);
        return false;
    }

    wstr<> wtext;
    if (!wtext.reserve(dwSize))
    {
        assert(false);
        return false;
    }

    if (!s_winhttp.WinHttpQueryHeaders(hRequest, dwInfoLevel, WINHTTP_HEADER_NAME_BY_INDEX, wtext.data(), &dwSize, WINHTTP_NO_HEADER_INDEX))
    {
        assert(false);
        return false;
    }

    out = wtext.c_str();
    return true;
}



//------------------------------------------------------------------------------
struct key_value_pair
{
    wstr_moveable key;
    wstr_moveable value;
};

//------------------------------------------------------------------------------
class httprequest_async_lua_task : public async_lua_task
{
public:
    httprequest_async_lua_task(const char* key, const char* src, async_yield_lua* asyncyield,
                               const char* method, const char* url, const char* user_agent, bool no_cache,
                               std::vector<key_value_pair>& headers,
                               const char* body, size_t body_len)
    : async_lua_task(key, src)
    , m_method(method)
    , m_url(url)
    , m_user_agent(user_agent)
    , m_headers(std::move(headers))
    , m_nocache(no_cache)
    , m_ismain(!asyncyield)
    {
        if (body && body_len)
        {
            if (body_len > 0x7fffffff)
            {
                m_body_len = -1;
            }
            else
            {
                m_body = static_cast<char*>(malloc(body_len));
                if (m_body)
                {
                    memcpy(m_body, body, body_len);
                    m_body_len = DWORD(body_len);
                }
                else
                {
                    m_body_len = -1;
                }
            }
        }
        set_asyncyield(asyncyield);
    }

    ~httprequest_async_lua_task()
    {
        free(m_result_buffer);
        free(m_body);
    }

    bool result(char*& buffer, size_t& length)
    {
        if (!is_complete())
            return false;
        buffer = m_result_buffer;
        length = m_result_size;
        return true;
    }

    const ::response_info& response_info() const
    {
        return m_response_info;
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
    const str_moveable m_method;
    const str_moveable m_url;
    const str_moveable m_user_agent;
    const std::vector<key_value_pair> m_headers;
    char* m_body = nullptr;
    DWORD m_body_len = 0;
    const bool m_nocache;
    const bool m_ismain;

    char* m_result_buffer = nullptr;
    size_t m_result_size = 0;
    ::response_info m_response_info;
};

//------------------------------------------------------------------------------
void httprequest_async_lua_task::do_work()
{
// TODO:  What if lua_state is recycled while this task thread is running?

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
        m_response_info.win32_error = GetLastError();
final_ret:
        wake_asyncyield();
        return;
    }

    wstr_moveable wua(m_user_agent.c_str());
    const WCHAR* wuser_agent = wua.empty() ? nullptr : wua.c_str();
    const DWORD access_type = IsWindows8OrGreater() ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    HINTERNET hSession = s_winhttp.WinHttpOpen(wuser_agent, access_type, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        m_response_info.win32_error = GetLastError();
        goto final_ret;
    }

    HINTERNET hConnect = s_winhttp.WinHttpConnect(hSession, comp.lpszHostName, comp.nPort, 0);
    if (!hConnect)
    {
        m_response_info.win32_error = GetLastError();
close_session:
        s_winhttp.WinHttpCloseHandle(hSession);
        goto final_ret;
    }

    DWORD flags = WINHTTP_FLAG_SECURE|WINHTTP_FLAG_ESCAPE_PERCENT;
    if (m_nocache)
        flags |= WINHTTP_FLAG_REFRESH;

    wstr<> wmethod(m_method.c_str());
    HINTERNET hRequest = s_winhttp.WinHttpOpenRequest(hConnect, wmethod.c_str(), comp.lpszUrlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest)
    {
        m_response_info.win32_error = GetLastError();
close_connect:
        s_winhttp.WinHttpCloseHandle(hConnect);
        goto close_session;
    }

    if (m_body_len == DWORD(-1))
    {
        m_response_info.win32_error = ERROR_INVALID_DATA;
        goto close_connect;
    }

    wstr_moveable headers;
    for (const auto& kv : m_headers)
    {
        bool ok = true;
        if (!headers.empty())
            headers.concat(L"\r\n");
        ok = ok && headers.concat(kv.key.c_str());
        headers.concat(L": ");
        ok = ok && headers.concat(kv.value.c_str());
        if (!ok || headers.length() > 0x7fff)
        {
            m_response_info.win32_error = ERROR_INVALID_PARAMETER;
            goto close_connect;
        }
    }

    const WCHAR* send_headers = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    BOOL bResults = s_winhttp.WinHttpSendRequest(hRequest, send_headers, headers.length(), m_body, m_body_len, 0, 0);
    if (!bResults)
    {
        m_response_info.win32_error = GetLastError();
close_request:
        s_winhttp.WinHttpCloseHandle(hRequest);
        goto close_connect;
    }

    bResults = s_winhttp.WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults)
    {
        m_response_info.win32_error = GetLastError();
        goto close_request;
    }

    m_response_info.init(hRequest);

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
        {
            m_response_info.win32_error = GetLastError();
            goto close_request;
        }

        // Finished reading all data.
        if (!dwDownloaded)
        {
            m_result_buffer = static_cast<char*>(malloc(m_result_size));
            if (!m_result_buffer)
            {
                m_response_info.win32_error = ERROR_NOT_ENOUGH_MEMORY;
                goto close_request;
            }

            char* write = m_result_buffer;
            size_t remaining = m_result_size;
            for (const auto& block : blocks)
            {
                const size_t to_copy = min<size_t>(c_block_size, remaining);
                memcpy(write, block, to_copy);
                write += to_copy;
                remaining -= to_copy;
            }
            m_response_info.completed_read = true;
            goto close_request;
        }

        assert(dwDownloaded <= c_block_size);
        assert(dwDownloaded <= available);
        available -= dwDownloaded;
        m_result_size += dwDownloaded;
    }
}

//------------------------------------------------------------------------------
class httprequest_lua
    : public lua_bindable<httprequest_lua>
{
public:
                        httprequest_lua(const std::shared_ptr<httprequest_async_lua_task>& task) : m_task(task) {}
                        ~httprequest_lua() {}

    int32               result(lua_State* state);

private:
    std::shared_ptr<httprequest_async_lua_task> m_task;

    friend class lua_bindable<httprequest_lua>;
    static const char* const c_name;
    static const httprequest_lua::method c_methods[];
};

//------------------------------------------------------------------------------
int32 httprequest_lua::result(lua_State* state)
{
    if (!m_task)
        return 0;

    // Return the response body.
    char* data = nullptr;
    size_t length = 0;
    if (m_task->result(data, length))
        lua_pushlstring(state, data, length);
    else
        lua_pushnil(state);

    // Return a table with status details.
    const response_info& info = m_task->response_info();
    lua_createtable(state, 0, 5);
    {
        if (info.win32_error)
        {
            lua_pushliteral(state, "win32_error");
            lua_pushinteger(state, info.win32_error);
            lua_rawset(state, -3);

            wstr_moveable wmsg;
            wmsg.reserve(4096);
            const DWORD FMW_flags = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
            const DWORD cch = FormatMessageW(FMW_flags, 0, info.win32_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wmsg.data(), wmsg.size(), nullptr);
            lua_pushliteral(state, "win32_error_text");
            if (cch && cch < wmsg.size())
            {
                str_moveable msg(wmsg.c_str());
                msg.trim();
                lua_pushlstring(state, msg.c_str(), msg.length());
            }
            else
            {
                lua_pushliteral(state, "Unknown error.");
            }
            lua_rawset(state, -3);
        }

        if (info.status_code)
        {
            lua_pushliteral(state, "status_code");
            lua_pushinteger(state, info.status_code);
            lua_rawset(state, -3);

            lua_pushliteral(state, "status_text");
            lua_pushlstring(state, info.status_text.c_str(), info.status_text.length());
            lua_rawset(state, -3);

            lua_pushliteral(state, "raw_headers");
            lua_pushlstring(state, info.raw_headers.c_str(), info.raw_headers.length());
            lua_rawset(state, -3);

            lua_pushliteral(state, "content_type");
            lua_pushlstring(state, info.content_type.c_str(), info.content_type.length());
            lua_rawset(state, -3);

            lua_pushliteral(state, "content_length");
            lua_pushinteger(state, info.content_length);
            lua_rawset(state, -3);
        }

        if (info.completed_read)
        {
            lua_pushliteral(state, "completed_read");
            lua_pushboolean(state, info.completed_read);
            lua_rawset(state, -3);
        }
    }

    return 2;
}

//------------------------------------------------------------------------------
const char* const httprequest_lua::c_name = "httprequest_lua";
const httprequest_lua::method httprequest_lua::c_methods[] = {
    { "result",         &result },
    {}
};



//------------------------------------------------------------------------------
static int32 http_request_internal(lua_State* state)
{
    int32 iarg = 1;
    const bool ismain = is_main_coroutine(state);
    const char* const method = checkstring(state, iarg++);
    const char* const url = checkstring(state, iarg++);
    const int32 iarg_options_table = lua_istable(state, iarg) ? iarg++ : -1;
    if (!method || !*method || !url || !*url)
        return 0;

    const char* user_agent = nullptr;
    bool no_cache = false;
    std::vector<key_value_pair> headers;
    const char* body = nullptr;
    size_t body_len = 0;
    if (iarg_options_table >= 0)
    {
        lua_pushvalue(state, iarg_options_table);

        lua_pushliteral(state, "user_agent");
        lua_rawget(state, -2);
        user_agent = optstring(state, -1, nullptr);
        lua_pop(state, 1);

        lua_pushliteral(state, "no_cache");
        lua_rawget(state, -2);
        no_cache = lua_toboolean(state, -1);
        lua_pop(state, 1);

        lua_pushliteral(state, "headers");
        lua_rawget(state, -2);
        if (lua_istable(state, -1))
        {
            lua_pushnil(state);
            while (lua_next(state, -2))
            {
                const char* key = nullptr;
                const char* value = nullptr;
                if (lua_isstring(state, -2))
                    key = lua_tostring(state, -2);
                if (lua_isstring(state, -1))
                    value = lua_tostring(state, -1);
                if (key && value)
                {
                    key_value_pair kv;
                    kv.key = key;
                    kv.value = value;
                    headers.emplace_back(std::move(kv));
                }
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);

        lua_pushliteral(state, "body");
        lua_rawget(state, -2);
        if (lua_isstring(state, -1))
            body = lua_tolstring(state, -1, &body_len);
        lua_pop(state, 1);
    }

    static uint32 s_counter = 0;
    str_moveable key;
    key.format("httprequest||%08x", ++s_counter);

    str<> src;
    get_lua_srcinfo(state, src);

    dbg_ignore_scope(snapshot, "async http request");

    // Push an asyncyield object.
    async_yield_lua* asyncyield = nullptr;
    if (ismain)
        lua_pushnil(state);
    else if ((asyncyield = async_yield_lua::make_new(state, "http.request")) == nullptr)
        return 0;

    // Push a task object.
    auto task = std::make_shared<httprequest_async_lua_task>(key.c_str(), src.c_str(), asyncyield, method, url, user_agent, no_cache, headers, body, body_len);
    if (!task)
        return 0;
    httprequest_lua* request = httprequest_lua::make_new(state, task);
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
        { "_request_internal",      &http_request_internal },
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
