// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_bindable.h"
#include "async_lua_task.h"
#include "yield.h"

#if 0

//------------------------------------------------------------------------------
// IShellDispatch

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <comdef.h>
#include <iostream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

bool ExtractZipWithShell(const std::wstring& zipPath, const std::wstring& destDir)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return false;

    bool success = false;

    IShellDispatch* pShell = nullptr;
    Folder* pZipFolder = nullptr;
    Folder* pDestFolder = nullptr;
    VARIANT vZip, vDest, vOpt, vItems;

    VariantInit(&vZip);
    VariantInit(&vDest);
    VariantInit(&vOpt);
    VariantInit(&vItems);

    // Create Shell.Application COM object
    hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pShell));
    if (SUCCEEDED(hr))
    {
        vZip.vt = VT_BSTR;
        vZip.bstrVal = SysAllocString(zipPath.c_str());
        hr = pShell->NameSpace(vZip, &pZipFolder);

        vDest.vt = VT_BSTR;
        vDest.bstrVal = SysAllocString(destDir.c_str());
        if (SUCCEEDED(hr))
            hr = pShell->NameSpace(vDest, &pDestFolder);

        if (SUCCEEDED(hr) && pZipFolder && pDestFolder)
        {
            FolderItems* pItems = nullptr;
            hr = pZipFolder->Items(&pItems);
            if (SUCCEEDED(hr) && pItems)
            {
                vItems.vt = VT_DISPATCH;
                vItems.pdispVal = pItems;

                vOpt.vt = VT_I4;
                vOpt.lVal = 0; // No options; could use FOF_NO_UI etc.

                hr = pDestFolder->CopyHere(vItems, vOpt);

                if (SUCCEEDED(hr))
                    success = true;

                pItems->Release();
            }
        }
    }

    // Cleanup
    if (vZip.vt == VT_BSTR) SysFreeString(vZip.bstrVal);
    if (vDest.vt == VT_BSTR) SysFreeString(vDest.bstrVal);
    if (pZipFolder) pZipFolder->Release();
    if (pDestFolder) pDestFolder->Release();
    if (pShell) pShell->Release();
    CoUninitialize();

    return success;
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        std::wcout << L"Usage: unzip <zipfile> <destination>\n";
        return 1;
    }

    std::wstring zipPath = argv[1];
    std::wstring destDir = argv[2];

    if (ExtractZipWithShell(zipPath, destDir))
        std::wcout << L"Extracted successfully.\n";
    else
        std::wcout << L"Extraction failed.\n";

    return 0;
}



//------------------------------------------------------------------------------
// miniz + compressapi

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/debugheap.h>
#include <assert.h>

#include <compressapi.h>
#include "miniz.h"

#pragma comment(lib, "Cabinet.lib")  // Compression API (Windows 8+)
#pragma comment(lib, "Shlwapi.lib")

bool ExtractZipWithWinCompression(const wchar_t* zipPath, const wchar_t* destDir)
{
    mz_zip_archive zip = {};
    std::string zipPathUtf8;

    // Convert path to UTF-8 for miniz
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, zipPath, -1, nullptr, 0, nullptr, nullptr);
        zipPathUtf8.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, zipPath, -1, zipPathUtf8.data(), len, nullptr, nullptr);
    }

    if (!mz_zip_reader_init_file(&zip, zipPathUtf8.c_str(), 0))
    {
        std::wcerr << L"Failed to open ZIP file: " << zipPath << std::endl;
        return false;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; i < numFiles; ++i)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;

        if (st.m_is_directory)
            continue;

        std::wstring entryName = Utf8ToWide(st.m_filename);
        std::wstring outPath = std::wstring(destDir) + L"\\" + entryName;

        // Create subdirectories
        EnsureDirectoriesExist(outPath);

        // --- Read compressed data ---
        std::vector<uint8_t> compData(st.m_comp_size);
        if (!mz_zip_reader_extract_to_mem_no_alloc(&zip, i, compData.data(),
                                                   compData.size(), 0, nullptr, 0))
        {
            std::wcerr << L"Failed to read compressed data: " << entryName << std::endl;
            continue;
        }

        // --- Decompress using Windows Compression API ---
        DECOMPRESSOR_HANDLE decomp = nullptr;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_DEFLATE, nullptr, &decomp))
        {
            std::wcerr << L"CreateDecompressor failed: " << GetLastError() << std::endl;
            continue;
        }

        SIZE_T outSize = 0;
        // First call determines the output buffer size.
        if (!Decompress(decomp, compData.data(), compData.size(), nullptr, 0, &outSize) &&
            GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            std::wcerr << L"Decompress size query failed for " << entryName << std::endl;
            CloseDecompressor(decomp);
            continue;
        }

        std::vector<uint8_t> outData(outSize);
        if (!Decompress(decomp, compData.data(), compData.size(), outData.data(), outSize, &outSize))
        {
            std::wcerr << L"Decompress failed for " << entryName << L": " << GetLastError() << std::endl;
            CloseDecompressor(decomp);
            continue;
        }

        CloseDecompressor(decomp);

        // --- Write decompressed file ---
        std::ofstream outFile(outPath, std::ios::binary);
        if (!outFile)
        {
            std::wcerr << L"Failed to open output file: " << outPath << std::endl;
            continue;
        }
        outFile.write(reinterpret_cast<const char*>(outData.data()), outSize);
        outFile.close();

        std::wcout << L"Extracted: " << entryName << std::endl;
    }

    mz_zip_reader_end(&zip);
    return true;
}

// Example usage
int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        std::wcout << L"Usage: unzip.exe <zipfile> <destdir>\n";
        return 0;
    }

    if (!ExtractZipWithWinCompression(argv[1], argv[2]))
    {
        std::wcerr << L"Extraction failed.\n";
        return 1;
    }

    std::wcout << L"Extraction complete.\n";
    return 0;
}





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



//------------------------------------------------------------------------------
static int32 close_file(lua_State *state)
{
    luaL_Stream* p = ((luaL_Stream*)luaL_checkudata(state, 1, LUA_FILEHANDLE));
    assert(p);
    if (!p)
        return 0;

    int32 res = fclose(p->f);
    return luaL_fileresult(state, (res == 0), NULL);
}



//------------------------------------------------------------------------------
struct execute_thread : public yield_thread
{
                    execute_thread(const char* command) : m_command(command) {}
                    ~execute_thread() {}
    int32           results(lua_State* state) override;
private:
    void            do_work() override;
    str_moveable    m_command;
    int32           m_stat = -1;
    errno_t         m_errno = 0;
};

//------------------------------------------------------------------------------
void execute_thread::do_work()
{
    m_stat = os::system(m_command.c_str(), get_cwd());
    m_errno = errno;
}

//------------------------------------------------------------------------------
int32 execute_thread::results(lua_State* state)
{
    errno = m_errno;
    return luaL_execresult(state, m_stat);
}



//------------------------------------------------------------------------------
static class delay_load_winhttp
{
public:
                        delay_load_winhttp();
    bool                init();
    //...
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    union
    {
        FARPROC         proc[4];
        struct {
            //...
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
            m_procs.proc[0] = GetProcAddress(hlib, "...");
        }

        m_ok = true;
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
int32 delay_load_winhttp::UuidCreate(UUID* out)
{
    if (init())
    {
        RPC_STATUS status = m_procs.UuidCreate(out);
        switch (status)
        {
        case RPC_S_OK:
            return 0;
        case RPC_S_UUID_LOCAL_ONLY:
        case RPC_S_UUID_NO_ADDRESS:
            return 1;
        }
    }
    return -1;
}



//------------------------------------------------------------------------------
class enumshares_async_lua_task : public async_lua_task
{
public:
    enumshares_async_lua_task(const char* key, const char* src, async_yield_lua* asyncyield, const char* server, bool hidden)
    : async_lua_task(key, src)
    , m_server(server)
    , m_hidden(hidden)
    , m_ismain(!asyncyield)
    {
        set_asyncyield(asyncyield);
    }

    bool next(str_base& out, bool* special=nullptr)
    {
        bool ret = false;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        // Fetch next available share.
        if (m_index < m_shares.size())
        {
            ret = true;
            out = m_shares[m_index].c_str() + 1;
            if (special)
                *special = (m_shares[m_index].c_str()[0] != '0');
            ++m_index;
            if (m_index >= m_shares.size() && !m_ismain)
                wake_asyncyield();
        }
        return ret;
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
    const str_moveable m_server;
    const bool m_hidden;
    const bool m_ismain;
    std::recursive_mutex m_mutex;
    std::vector<str_moveable> m_shares;
    size_t m_index = 0;
};

//------------------------------------------------------------------------------
void enumshares_async_lua_task::do_work()
{
    static bool s_has_netapi = delayload_netapi();
    if (!s_has_netapi)
        return;

    wstr<> wserver(m_server.c_str());
    LMSTR lmserver = const_cast<LMSTR>(wserver.c_str());
    PSHARE_INFO_1 buffer = nullptr;
    DWORD entries_read;
    DWORD total_entries;
    DWORD resume_handle = 0;
    NET_API_STATUS res = ERROR_MORE_DATA;
    while (!is_canceled() && res == ERROR_MORE_DATA)
    {
        res = s_netapi.NetShareEnum(lmserver, 1, (LPBYTE*)&buffer, MAX_PREFERRED_LENGTH, &entries_read, &total_entries, &resume_handle);
        if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA)
        {
#undef DEBUG_SLEEP
//#define DEBUG_SLEEP
            {
#ifndef DEBUG_SLEEP
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
#endif
                for (PSHARE_INFO_1 info = buffer; entries_read--; ++info)
                {
                    const bool special = !!(info->shi1_type & STYPE_SPECIAL);
                    if (special && !g_files_hidden.get())
                        continue;
                    if ((info->shi1_type & STYPE_MASK) == STYPE_DISKTREE)
                    {
#ifdef DEBUG_SLEEP
Sleep(1000);
std::lock_guard<std::recursive_mutex> lock(m_mutex);
#endif
                        dbg_ignore_scope(snapshot, "async enum shares");
                        wstr_moveable netname;
                        netname.format(L"%c%s", special ? '1' : '0', info->shi1_netname);
                        m_shares.emplace_back(netname.c_str());
#ifdef DEBUG_SLEEP
wake_asyncyield();
#endif
                    }
                }
            }
            s_netapi.NetApiBufferFree(buffer);

            wake_asyncyield();
        }
    };

    wake_asyncyield();
}

//------------------------------------------------------------------------------
class enumshares_lua
    : public lua_bindable<enumshares_lua>
{
public:
                        enumshares_lua(const std::shared_ptr<enumshares_async_lua_task>& task) : m_task(task) {}
                        ~enumshares_lua() {}

    bool                next(str_base& out) { return m_task->next(out); }

    void                pushcclosure(lua_State* state) { lua_pushcclosure(state, iter_aux, 2); }

protected:
    static int32        iter_aux(lua_State* state);

private:
    std::shared_ptr<enumshares_async_lua_task> m_task;

    friend class lua_bindable<enumshares_lua>;
    static const char* const c_name;
    static const enumshares_lua::method c_methods[];
};

//------------------------------------------------------------------------------
inline bool is_main_coroutine(lua_State* state) { return G(state)->mainthread == state; }

//------------------------------------------------------------------------------
int32 enumshares_lua::iter_aux(lua_State* state)
{
    int ctx = 0;
    if (lua_getctx(state, &ctx) == LUA_YIELD && ctx)
    {
        // Resuming from yield; remove asyncyield.
        lua_state::push_named_function(state, "clink._set_coroutine_asyncyield");
        lua_pushnil(state);
        lua_state::pcall_silent(state, 1, 0);
    }

    auto* async = async_yield_lua::test(state, lua_upvalueindex(1));
    auto* self = check(state, lua_upvalueindex(2));
    assert(self);
    if (!self)
        return 0;

    str<> out;
    bool special = false;
    if (self->m_task->next(out, &special))
    {
        // Return next share name.
        lua_pushlstring(state, out.c_str(), out.length());
        lua_pushboolean(state, special);
        if (self->m_task->is_canceled())
            lua_pushliteral(state, "canceled");
        else
            lua_pushnil(state);
        return 3;
    }

    if (async && async->is_expired())
    {
        self->m_task->cancel();
        return 0;
    }
    else if (!self->m_task->is_complete() && !self->m_task->is_canceled())
    {
        assert(!is_main_coroutine(state));
        assert(async);

        // No more shares available yet.
        async->clear_ready();

        // Yielding; set asyncyield.
        lua_state::push_named_function(state, "clink._set_coroutine_asyncyield");
        lua_pushvalue(state, lua_upvalueindex(1));
        lua_state::pcall_silent(state, 1, 0);

        // Yield.
        self->push(state);
        return lua_yieldk(state, 1, 1, iter_aux);
    }

    return 0;
}

//------------------------------------------------------------------------------
const char* const enumshares_lua::c_name = "enumshares_lua";
const enumshares_lua::method enumshares_lua::c_methods[] = {
    {}
};

//------------------------------------------------------------------------------
/// -name:  http.get
/// -ver:   1.9.0
/// -arg:   ?
/// -ret:   ?
static int32 http_get(lua_State* state)
{
    int32 iarg = 1;
    const bool ismain = is_main_coroutine(state);
    const char* const server = checkstring(state, iarg++);
    const bool hidden = lua_isboolean(state, iarg) && lua_toboolean(state, iarg++);
    const auto _timeout = optnumber(state, iarg++, 0);
    const auto _timeoutms = _timeout * 1000;
    const uint32 timeout = (_timeout < 0 ||
                            _timeoutms >= double(uint32(INFINITE)) ||
                            (_timeout <= 0 && ismain)) ? INFINITE : uint32(_timeoutms);
    if (!server || !*server)
        return 0;

    static uint32 s_counter = 0;
    str_moveable key;
    key.format("enumshares||%08x", ++s_counter);

    str<> src;
    get_lua_srcinfo(state, src);

    dbg_ignore_scope(snapshot, "async enum shares");

    // Push an asyncyield object.
    async_yield_lua* asyncyield = nullptr;
    if (ismain)
    {
        lua_pushnil(state);
    }
    else
    {
        asyncyield = async_yield_lua::make_new(state, "os.enumshares", timeout);
        if (!asyncyield)
            return 0;
    }

    // Push a task object.
    auto task = std::make_shared<enumshares_async_lua_task>(key.c_str(), src.c_str(), asyncyield, server, hidden);
    if (!task)
        return 0;
    enumshares_lua* es = enumshares_lua::make_new(state, task);
    if (!es)
        return 0;
    {
        std::shared_ptr<async_lua_task> add(task); // Because MINGW can't handle it inline.
        add_async_lua_task(add);
    }

    // If a timeout was given and this is the main coroutine, wait for
    // completion until the timeout, and then cancel the task if not yet
    // complete.
    if (ismain && timeout)
    {
        if (!task->wait(timeout))
            task->cancel();
    }

    // es was already pushed by make_new.
    // asyncyield was already pushed by make_new.
    es->pushcclosure(state);
    return 1;
}



//------------------------------------------------------------------------------
void http_lua_initialise(lua_state& lua)
{
    static const struct {
        const char* name;
        int32       (*method)(lua_State*);
    } methods[] = {
        { "get",         &http_get },
    };

    lua_State* state = lua.get_state();

    lua_getglobal(state, "http");

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_pop(state, 1);
}

#endif
