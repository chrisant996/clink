// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_input_idle.h"
#include "line_state_lua.h"
#include "line_states_lua.h"
#include "prompt.h"
#include "async_lua_task.h"
#include "command_link_dialog.h"
#include "sessionstream.h"
#include "../../app/src/version.h" // Ugh.

#include <core/base.h>
#include <core/os.h>
#include <core/cwd_restorer.h>
#include <core/str_compare.h>
#include <core/str_transform.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/linear_allocator.h>
#include <core/callstack.h>
#include <core/debugheap.h>
#include <lib/popup.h>
#include <lib/cmd_tokenisers.h>
#include <lib/reclassify.h>
#include <lib/recognizer.h>
#include <lib/matches_lookaside.h>
#include <lib/line_editor_integration.h>
#include <lib/rl_integration.h>
#include <lib/suggestions.h>
#include <lib/slash_translation.h>
#include <lib/host_callbacks.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <terminal/screen_buffer.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <wintrust.h>
#include <mscat.h>
#include <softpub.h>

extern "C" {
#include <lua.h>
#include <readline/history.h>
}

#include <share.h>
#include <mutex>



//------------------------------------------------------------------------------
extern setting_enum g_dupe_mode;
extern setting_bool g_lua_breakonerror;
extern setting_bool g_match_wild;

#ifdef _WIN64
static const char c_uninstall_key[] = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#else
static const char c_uninstall_key[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#endif



//------------------------------------------------------------------------------
// These are implemented in os_api.cpp.
extern int32 api_glob_dirs(lua_State* state);
extern int32 api_glob_files(lua_State* state);
extern int32 globber_impl(lua_State* state, bool dirs_only, bool back_compat=false);

//------------------------------------------------------------------------------
#pragma region Updater Helpers

const uint32 c_snooze_duration = 22 * 60 * 60;  // Effectively one day, but avoids the threshold creeping forward over time.
static HANDLE s_hMutex = 0;

static bool acquire_updater_mutex()
{
    if (s_hMutex)
        return true;

    wstr<280> mod;
    const DWORD mod_len = GetModuleFileNameW(nullptr, mod.data(), mod.size());
    if (!mod_len || mod_len >= mod.size())
        return false;

    // Only allow the standalone clink exe to acquire the mutex, to avoid
    // blocking inside cmd.exe.
    str<16> name(path::get_name(mod.c_str()));
    if (_strnicmp("clink_", name.c_str(), 6) != 0)
    {
        assert(false);
        return false;
    }

    // Acquire a shared named mutex to synchronize all update attempts.  Since
    // this can never happen inside cmd.exe, it's reasonable to "leak" the
    // mutex so that when this "clink update" process finishes, the next
    // waiting one can continue.  This greatly simplifies managing the
    // acquisition and release of the mutex.
    HANDLE hMutex = CreateMutex(nullptr, false, "clink_autoupdate_global_serializer");
    if (hMutex)
    {
        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0)
        {
            CloseHandle(hMutex);
            return false;
        }
        s_hMutex = hMutex;
    }

    return true;
}

static void release_updater_mutex()
{
    if (s_hMutex)
    {
        ReleaseMutex(s_hMutex);
        CloseHandle(s_hMutex);
        s_hMutex = 0;
    }
}

static bool make_key_value_names(const char* subkey, wstr_base& keyname, wstr_base& valname)
{
    str<280> tmp1;
    if (!os::get_alias("clink", tmp1))
        return false;

    str<280> tmp2;
    if (!path::get_directory(tmp1.c_str(), tmp2))
        return false;

    if (!tmp1.format("Software\\Clink\\%s", subkey))
        return false;

    keyname = tmp1.c_str();
    valname = tmp2.c_str();
    return true;
}

static int32 encode_version(const char* str)
{
    int32 version = 0;
    if (str && 'v' == *(str++))
    {
        char* end;
        const int32 major = strtol(str, &end, 10);
        if (end && *end == '.')
        {
            version = major;
            const int32 minor = strtol(end + 1, &end, 10);
            if (end && *end == '.')
            {
                version *= 1000;
                version += minor;
                const int32 patch = strtol(end + 1, &end, 10);
                if (end && (*end == '.' || !*end))
                {
                    version *= 10000;
                    version += patch;
                    return version;
                }
            }
        }

    }
    return 0;
}

static bool is_update_skipped(const char* new_ver, str_base* reg_ver=nullptr)
{
    const int32 candidate = encode_version(new_ver);
    if (!candidate)
        return false;

    wstr<> keyname;
    wstr<> valname;
    if (!make_key_value_names("SkipUpdate", keyname, valname))
        return false;

    DWORD type;
    WCHAR buffer[280];
    DWORD size = sizeof(buffer);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, keyname.c_str(), valname.c_str(), RRF_RT_REG_SZ, &type, buffer, &size);
    if (status != ERROR_SUCCESS || type != REG_SZ)
        return false;

    str<16> tmp;
    if (!reg_ver)
        reg_ver = &tmp;
    *reg_ver = buffer;

    const int32 skip = encode_version(reg_ver->c_str());
    if (!candidate || !skip || skip < candidate)
        return false;

    return true;
}

static bool is_update_prompt_snoozed()
{
    wstr<> keyname;
    wstr<> valname;
    if (make_key_value_names("SnoozeUpdatePrompt", keyname, valname))
    {
        DWORD type;
        WCHAR buffer[280];
        DWORD size = sizeof(buffer);
        LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, keyname.c_str(), valname.c_str(), RRF_RT_REG_SZ, &type, buffer, &size);
        if (status == ERROR_SUCCESS && type == REG_SZ)
        {
            str<16> snooze(buffer);
            time_t until = atoi(snooze.c_str());
            time_t now = time(nullptr);
            if (until > 0 && now > 0 && now < until)
                return true;
        }
    }
    return false;
}

static void snooze_update_prompt()
{
    wstr<> keyname;
    wstr<> valname;
    if (!make_key_value_names("SnoozeUpdatePrompt", keyname, valname))
        return;

    HKEY hkey;
    DWORD dwDisposition;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, keyname.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, nullptr, &hkey, &dwDisposition);
    if (status != ERROR_SUCCESS)
        return;

    wstr<> snooze;
    time_t until = time(nullptr) + c_snooze_duration;
    snooze.format(L"%u", until);
    status = RegSetValueExW(hkey, valname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(snooze.c_str()), snooze.length() * sizeof(*snooze.c_str()));

    RegCloseKey(hkey);
}

static void skip_update(const char* ver)
{
    wstr<> keyname;
    wstr<> valname;
    if (!make_key_value_names("SkipUpdate", keyname, valname))
        return;

    HKEY hkey;
    DWORD dwDisposition;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, keyname.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, nullptr, &hkey, &dwDisposition);
    if (status != ERROR_SUCCESS)
        return;

    wstr<> wver(ver);
    status = RegSetValueExW(hkey, valname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(wver.c_str()), wver.length() * sizeof(*wver.c_str()));

    RegCloseKey(hkey);
}

#pragma endregion Updater Helpers

//------------------------------------------------------------------------------
#pragma region Path Type Cache Helpers

static str_unordered_map<int32> s_cached_path_type;
static linear_allocator s_cached_path_store(2048);
static std::recursive_mutex s_cached_path_type_mutex;
void clear_path_type_cache()
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    s_cached_path_type.clear();
    s_cached_path_store.clear();
}
void add_cached_path_type(const char* full, int32 type)
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    dbg_ignore_scope(snapshot, "add_cached_path_type");
    uint32 size = uint32(strlen(full) + 1);
    char* key = static_cast<char*>(s_cached_path_store.alloc(size));
    memcpy(key, full, size);
    s_cached_path_type.emplace(key, type);
}
bool get_cached_path_type(const char* full, int32& type)
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    const auto& iter = s_cached_path_type.find(full);
    if (iter == s_cached_path_type.end())
        return false;
    type = iter->second;
    return true;
}

class path_type_async_lua_task : public async_lua_task
{
public:
    path_type_async_lua_task(const char* key, const char* src, const char* path)
    : async_lua_task(key, src)
    , m_path(path)
    {}

    int32 get_path_type() const { return m_type; }

protected:
    void do_work() override
    {
        m_type = os::get_path_type(m_path.c_str());
        add_cached_path_type(m_path.c_str(), m_type);
    }

private:
    str_moveable m_path;
    int32 m_type = os::path_type_invalid;
};

#pragma endregion Path Type Cache Helpers

//------------------------------------------------------------------------------
#pragma region Unzip Helpers

class unzip_async_lua_task : public async_lua_task
{
public:
    unzip_async_lua_task(const char* key, const char* src, async_yield_lua* asyncyield,
                         const char* zip, const char* dest)
    : async_lua_task(key, src)
    , m_zip(zip)
    , m_dest(dest)
    , m_ismain(!asyncyield)
    {
        set_asyncyield(asyncyield);
    }

    ~unzip_async_lua_task()
    {
    }

    const char* get_error_message() const
    {
        if (is_complete() && !m_err_msg.empty())
            return m_err_msg.c_str();
        return nullptr;
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
    const str_moveable m_zip;
    const str_moveable m_dest;
    str_moveable m_err_msg;
    HRESULT m_hr = E_UNEXPECTED;
    const bool m_ismain;
};

void unzip_async_lua_task::do_work()
{
    m_hr = os::shell_unzip(m_zip.c_str(), m_dest.c_str(), m_err_msg);
    wake_asyncyield();
}

class unzip_lua
    : public lua_bindable<unzip_lua>
{
public:
                        unzip_lua(const std::shared_ptr<unzip_async_lua_task>& task) : m_task(task) {}
                        ~unzip_lua() {}

    int32               result(lua_State* state);

private:
    std::shared_ptr<unzip_async_lua_task> m_task;

    friend class lua_bindable<unzip_lua>;
    static const char* const c_name;
    static const unzip_lua::method c_methods[];
};

int32 unzip_lua::result(lua_State* state)
{
    if (!m_task)
        return 0;

    if (!m_task->is_complete())
        return 0;

    if (m_task->get_error_message())
    {
        lua_pushnil(state);
        lua_pushstring(state, m_task->get_error_message());
        return 2;
    }

    lua_pushboolean(state, true);
    return 1;
}

const char* const unzip_lua::c_name = "unzip_lua";
const unzip_lua::method unzip_lua::c_methods[] = {
    { "result",         &result },
    {}
};

#pragma endregion Unzip Helpers

//------------------------------------------------------------------------------
#pragma region WinTrust Helpers

static class delay_load_wintrust
{
public:
                        delay_load_wintrust();
    bool                init();
    HMODULE             module() const { return m_hlib; }
    LONG                WinVerifyTrust(HWND hwnd, GUID* pgActionID, LPVOID pWVTData);
    BOOL                CryptCATAdminAcquireContext(HCATADMIN* phCatAdmin, const GUID* pgSubsystem, DWORD dwFlags);
    BOOL                CryptCATAdminReleaseContext(HCATADMIN hCatAdmin, DWORD dwFlags);
    BOOL                CryptCATAdminCalcHashFromFileHandle(HANDLE hFile, DWORD* pcbHash, BYTE* pbHash, DWORD dwFlags);
private:
    bool                m_initialized = false;
    bool                m_ok = false;
    HMODULE             m_hlib = 0;
    union
    {
        FARPROC         proc[4];
        struct {
            LONG        (WINAPI* WinVerifyTrust)(HWND hwnd, GUID* pgActionID, LPVOID pWVTData);
            BOOL        (WINAPI* CryptCATAdminAcquireContext)(HCATADMIN* phCatAdmin, const GUID* pgSubsystem, DWORD dwFlags);
            BOOL        (WINAPI* CryptCATAdminReleaseContext)(HCATADMIN hCatAdmin, DWORD dwFlags);
            BOOL        (WINAPI* CryptCATAdminCalcHashFromFileHandle)(HANDLE hFile, DWORD* pcbHash, BYTE* pbHash, DWORD dwFlags);
        } proto;
    } m_procs;
} s_wintrust;

delay_load_wintrust::delay_load_wintrust()
{
    ZeroMemory(&m_procs, sizeof(m_procs));
}

bool delay_load_wintrust::init()
{
    if (!m_initialized)
    {
        m_initialized = true;
        m_hlib = LoadLibrary("wintrust.dll");
        if (m_hlib)
        {
            size_t c = 0;
            m_procs.proc[c++] = GetProcAddress(m_hlib, "WinVerifyTrust");
            m_procs.proc[c++] = GetProcAddress(m_hlib, "CryptCATAdminAcquireContext");
            m_procs.proc[c++] = GetProcAddress(m_hlib, "CryptCATAdminReleaseContext");
            m_procs.proc[c++] = GetProcAddress(m_hlib, "CryptCATAdminCalcHashFromFileHandle");
            assert(_countof(m_procs.proc) == c);
        }

        m_ok = true;
        static_assert(sizeof(m_procs.proc) == sizeof(m_procs.proto), "proc vs proto mismatch");
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
    if (!m_ok)
        SetLastError(ERROR_ACCESS_DENIED);  // TODO: Choose a better error.

    return m_ok;
}

LONG delay_load_wintrust::WinVerifyTrust(HWND hwnd, GUID* pgActionID, LPVOID pWVTData)
{
    if (!init())
        return ERROR_ACCESS_DENIED;         // TODO: Choose a better error.
    return m_procs.proto.WinVerifyTrust(hwnd, pgActionID, pWVTData);
}

BOOL delay_load_wintrust::CryptCATAdminAcquireContext(HCATADMIN* phCatAdmin, const GUID* pgSubsystem, DWORD dwFlags)
{
    if (!init())
        return false;
    return m_procs.proto.CryptCATAdminAcquireContext(phCatAdmin, pgSubsystem, dwFlags);
}

BOOL delay_load_wintrust::CryptCATAdminReleaseContext(HCATADMIN hCatAdmin, DWORD dwFlags)
{
    if (!init())
        return false;
    return m_procs.proto.CryptCATAdminReleaseContext(hCatAdmin, dwFlags);
}

BOOL delay_load_wintrust::CryptCATAdminCalcHashFromFileHandle(HANDLE hFile, DWORD* pcbHash, BYTE* pbHash, DWORD dwFlags)
{
    if (!init())
        return false;
    return m_procs.proto.CryptCATAdminCalcHashFromFileHandle(hFile, pcbHash, pbHash, dwFlags);
}

static void format_message_from_system(LONG code, str_base& out, const char* tag1=nullptr, const char* tag2=nullptr)
{
    wstr_moveable wmsg;
    wmsg.reserve(4096);

    const DWORD FMW_flags = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD cch = FormatMessageW(FMW_flags, 0, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wmsg.data(), wmsg.size(), nullptr);
    if (cch && cch < wmsg.size())
    {
        out.clear();
        if (tag1)
        {
            out.concat(tag1);
            out.concat(": ");
        }
        if (tag2)
        {
            out.concat(tag2);
            out.concat(": ");
        }
        to_utf8(out, wmsg.c_str());
        out.trim();
    }
    else
    {
        out.format("Unknown error 0x%x.", code);
    }
}

static bool win_verify_trust_file(const char* name, str_base& msg)
{
    wstr_moveable wname(name);

    WINTRUST_FILE_INFO file_info = { sizeof(file_info) };
    file_info.pcwszFilePath = wname.c_str();

    WINTRUST_DATA wintrust_data = { sizeof(wintrust_data) };
    wintrust_data.dwUIChoice = WTD_UI_NONE;
    wintrust_data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    wintrust_data.dwUnionChoice = WTD_CHOICE_FILE;
    wintrust_data.dwStateAction = WTD_STATEACTION_VERIFY;
    wintrust_data.pFile = &file_info;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG lStatus = s_wintrust.WinVerifyTrust(nullptr, &action, &wintrust_data);

    if (ERROR_SUCCESS != lStatus)
        format_message_from_system(lStatus, msg, name);

    wintrust_data.dwStateAction = WTD_STATEACTION_CLOSE;
    s_wintrust.WinVerifyTrust(nullptr, &action, &wintrust_data);

    return (ERROR_SUCCESS == lStatus);
}

static bool win_verify_trust_catalog(const char* catalog, const char* name, str_base& msg)
{
    wstr_moveable wcatalog(catalog);
    wstr_moveable wname(name);

    bool verified = false;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hCatAdmin = 0;

    hFile = CreateFileW(wname.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        format_message_from_system(GetLastError(), msg, "open", name);
ret:
        if (hCatAdmin)
            s_wintrust.CryptCATAdminReleaseContext(hCatAdmin, 0);
        if (INVALID_HANDLE_VALUE != hFile)
            CloseHandle(hFile);
        return verified;
    }

    if (!s_wintrust.CryptCATAdminAcquireContext(&hCatAdmin, nullptr, 0))
    {
        format_message_from_system(GetLastError(), msg, "catalog");
        goto ret;
    }

    BYTE hash_bytes[32];
    DWORD hash_len = sizeof(hash_bytes);
    if (!s_wintrust.CryptCATAdminCalcHashFromFileHandle(hFile, &hash_len, hash_bytes, 0))
    {
        format_message_from_system(GetLastError(), msg, "hash", name);
        goto ret;
    }

    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    // WCHAR whash[1 + _countof(hash_bytes) * 2];
    // for (DWORD i = 0; i < hash_len; ++i)
    //     swprintf_s(&whash[i * 2], 3, L"%02X", hash_bytes[i]);

    WINTRUST_CATALOG_INFO catalog_info = { sizeof(catalog_info) };
    catalog_info.pcwszCatalogFilePath = wcatalog.c_str();
    catalog_info.pcwszMemberFilePath = wname.c_str();
    catalog_info.pbCalculatedFileHash = hash_bytes;
    catalog_info.cbCalculatedFileHash = hash_len;
    // catalog_info.pcwszMemberTag = whash;
    // catalog_info.hCatAdmin = hCatAdmin;

    WINTRUST_DATA wintrust_data = { sizeof(wintrust_data) };
    wintrust_data.dwUIChoice = WTD_UI_NONE;
    wintrust_data.dwUnionChoice = WTD_CHOICE_CATALOG;
    wintrust_data.dwStateAction = WTD_STATEACTION_VERIFY;
    wintrust_data.pCatalog = &catalog_info;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG lStatus = s_wintrust.WinVerifyTrust(nullptr, &action, &wintrust_data);

    if (ERROR_SUCCESS != lStatus)
        format_message_from_system(lStatus, msg, catalog, name);

    wintrust_data.dwStateAction = WTD_STATEACTION_CLOSE;
    s_wintrust.WinVerifyTrust(nullptr, &action, &wintrust_data);

    verified = (ERROR_SUCCESS == lStatus);
    goto ret;
}

#pragma endregion WinTrust Helpers



//------------------------------------------------------------------------------
static int32 history_suggester(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const bool firstword = lua_toboolean(state, 2);
    const bool has_limit = !lua_isnoneornil(state, 3);
    int32 limit = has_limit ? checkinteger(state, 3).get() : -1;
    const int32 match_prev_cmd = lua_toboolean(state, 4);
    if (!line || (has_limit && limit <= 0))
        return 0;

    HIST_ENTRY** history = history_list();
    if (!history || history_length <= 0)
        return 0;

    // 'match_prev_cmd' only works when 'history.dupe_mode' is 'add'.
    if (match_prev_cmd && g_dupe_mode.get() != 0)
        return 0;

    // Make 'history' never contribute more than 10 suggestions.
    if (has_limit)
        limit = min(limit, 10);

    const char* prev_cmd = (match_prev_cmd && history_length > 0) ? history[history_length - 1]->line : nullptr;

    bool substr = false;
    int32 n = 0;
    lua_createtable(state, has_limit ? limit : 1, 0);

again:
    const DWORD tick = GetTickCount();
    const int32 scan_min = 100;
    const DWORD ms_max = substr ? 25 : 25;

    int32 scanned = 0;
    for (int32 i = history_length; --i >= 0;)
    {
        // Search at least SCAN_MIN entries.  But after that don't keep going
        // unless it's been less than MS_MAX milliseconds.
        if (scanned >= scan_min && !(scanned % 20) && GetTickCount() - tick >= ms_max)
            break;
        scanned++;

        int32 offset;
        int32 matchlen;
        if (substr)
        {
            offset = 0;
            matchlen = 0;
            for (const char* hline = history[i]->line; *hline; ++hline, ++offset)
            {
                str_iter lhs(line);
                str_iter rhs(hline);
                const int32 sublen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(lhs, rhs);
                if (sublen && !lhs.more() && (rhs.more() || sublen < 0))
                {
                    ++offset; // Convert from 0-based to 1-based.
                    matchlen = (sublen < 0) ? str_len(hline) : sublen;
                    break;
                }
            }
        }
        else
        {
            str_iter lhs(line);
            str_iter rhs(history[i]->line);
            matchlen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(lhs, rhs);
            offset = 1;

            // lhs isn't exhausted, or rhs is exhausted?  Continue searching.
            if (lhs.more() || !rhs.more())
                continue;
        }

        // Zero matching length?  Is ok with 'match_prev_cmd', otherwise
        // continue searching.
        if (!matchlen && !match_prev_cmd)
            continue;

        // Match previous command, if needed.
        if (match_prev_cmd)
        {
            if (i <= 0 || str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(prev_cmd, history[i - 1]->line) != -1)
                continue;
        }

        // Suggest this history entry.
        lua_createtable(state, 0, 2);

        lua_pushstring(state, history[i]->line);
        lua_rawseti(state, -2, 1);

        lua_pushinteger(state, 1);
        lua_rawseti(state, -2, 2);

        lua_pushliteral(state, "highlight");
        lua_createtable(state, 2, 0);
        {
            lua_pushinteger(state, offset);
            lua_rawseti(state, -2, 1);
            lua_pushinteger(state, matchlen);
            lua_rawseti(state, -2, 2);
        }
        lua_rawset(state, -3);

        lua_pushliteral(state, "history");
        lua_pushinteger(state, i + 1);
        lua_rawset(state, -3);

        lua_rawseti(state, -2, ++n);
        if (n >= limit)
            break;
    }

    if (n)
        return 1;

    // If collecting suggestions for the suggestion list and no prefix match
    // found in first pass, do a second pass looking for substring matches.
    if (has_limit && !match_prev_cmd && !substr)
    {
        substr = true;
        goto again;
    }

    lua_pop(state, 1);

    return 0;
}

//------------------------------------------------------------------------------
static int32 set_suggestion_started(lua_State* state)
{
    const char* line = checkstring(state, 1);
    if (!line)
        return 0;

    set_suggestion_started(line);
    return 0;
}

//------------------------------------------------------------------------------
static int32 set_suggestion_result(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const auto _endword_offset = checkinteger(state, 2);
    if (!line || !_endword_offset.isnum() || !lua_istable(state, 3))
        return 0;

    const int32 line_len = str_len(line);
    const int32 endword_offset = _endword_offset - 1;
    if (endword_offset < 0 || endword_offset > line_len)
        return 0;

    suggestions suggestions;

    if (lua_istable(state, 3))
    {
        lua_pushvalue(state, 3);

        const int32 len = int32(lua_rawlen(state, -1));
        for (int32 idx = 1; idx <= len; ++idx)
        {
            lua_rawgeti(state, -1, idx);

            if (lua_istable(state, -1))
            {
                lua_rawgeti(state, -1, 1);
                const char* suggestion = optstring(state, -1, nullptr);
                lua_pop(state, 1);

                lua_rawgeti(state, -1, 2);
                const auto _offset = optinteger(state, -1, 0);
                if (!_offset.isnum())
                    return 0;
                lua_pop(state, 1);

                int32 hs = -1;
                int32 hl = -1;
                lua_pushliteral(state, "highlight");
                lua_rawget(state, -2);
                if (lua_istable(state, -1))
                {
                    lua_rawgeti(state, -1, 1);
                    hs = optinteger(state, -1, 0) - 1;
                    lua_pop(state, 1);
                    lua_rawgeti(state, -1, 2);
                    hl = optinteger(state, -1, 0);
                    lua_pop(state, 1);
                    if (hs < 0 || hl < 0)
                        hs = hl = -1;
                }
                lua_pop(state, 1);

                lua_pushliteral(state, "tooltip");
                lua_rawget(state, -2);
                const char* tooltip = optstring(state, -1, nullptr);
                lua_pop(state, 1);

                lua_pushliteral(state, "source");
                lua_rawget(state, -2);
                const char* source = optstring(state, -1, nullptr);
                lua_pop(state, 1);

                int32 history = -1;
                if (source && !strcmp(source, "history"))
                {
                    lua_pushliteral(state, "history");
                    lua_rawget(state, -2);
                    const auto _history = optinteger(state, -1, -1);
                    lua_pop(state, 1);
                    history = _history - 1;
                    if (history < 0 || history >= history_length)
                        history = -1;
                }

                int32 offset = _offset - 1;
                if (offset < 0 || offset > line_len)
                    offset = line_len;
                if (!source || !*source)
                    source = "unknown";

                suggestions.add(suggestion, offset, source, hs, hl, tooltip, history);
            }

            lua_pop(state, 1);
        }

        lua_pop(state, 1);
    }

    set_suggestions(line, endword_offset, &suggestions);
    return 0;
}

//------------------------------------------------------------------------------
static int32 is_suggestionlist_mode(lua_State* state)
{
    lua_pushboolean(state, is_suggestion_list_active(true/*even_if_hidden*/));
    return 1;
}



//------------------------------------------------------------------------------
static int32 acquire_updater_mutex(lua_State* state)
{
    lua_pushboolean(state, acquire_updater_mutex());
    return 1;
}

//------------------------------------------------------------------------------
static int32 release_updater_mutex(lua_State* state)
{
    release_updater_mutex();
    return 0;
}

//------------------------------------------------------------------------------
static int32 get_installation_type(lua_State* state)
{
    // Open the Uninstall key.

    HKEY hkey;
    wstr<> where(c_uninstall_key);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey))
    {
failed:
        lua_pushliteral(state, "zip");
        return 1;
    }

    // Get binaries path.

    WCHAR long_bin_dir[MAX_PATH * 2];
    {
        str<> tmp;
        if (!os::get_env("=clink.bin", tmp))
            goto failed;

        wstr<> bin_dir(tmp.c_str());
        DWORD len = GetLongPathNameW(bin_dir.c_str(), long_bin_dir, sizeof_array(long_bin_dir));
        if (!len || len >= sizeof_array(long_bin_dir))
            goto failed;

        long_bin_dir[len] = '\0';
    }

    // Enumerate installed programs.

    bool found = false;
    WCHAR install_key[MAX_PATH];
    install_key[0] = '\0';

    for (DWORD index = 0; true; ++index)
    {
        DWORD size = sizeof_array(install_key); // Characters, not bytes, for RegEnumKeyExW.
        if (ERROR_NO_MORE_ITEMS == RegEnumKeyExW(hkey, index, install_key, &size, 0, nullptr, nullptr, nullptr))
            break;

        if (size >= sizeof_array(install_key))
            size = sizeof_array(install_key) - 1;
        install_key[size] = '\0';

        // Ignore if not a Clink installation.
        if (_wcsnicmp(install_key, L"clink_", 6))
            continue;

        HKEY hsubkey;
        if (RegOpenKeyExW(hkey, install_key, 0, MAXIMUM_ALLOWED, &hsubkey))
            continue;

        DWORD type;
        WCHAR location[280];
        DWORD len = sizeof(location); // Bytes, not characters, for RegQueryValueExW.
        LSTATUS status = RegQueryValueExW(hsubkey, L"InstallLocation", NULL, &type, LPBYTE(&location), &len);
        RegCloseKey(hsubkey);

        if (status)
            continue;

        len = len / 2;
        if (len >= sizeof_array(location))
            continue;
        location[len] = '\0';

        // If the uninstall location matches the current binaries directory,
        // then this is a match.
        WCHAR long_location[MAX_PATH * 2];
        len = GetLongPathNameW(location, long_location, sizeof_array(long_location));
        if (len && len < sizeof_array(long_location) && !_wcsicmp(long_bin_dir, long_location))
        {
            found = true;
            break;
        }
    }

    RegCloseKey(hkey);

    if (!found)
        goto failed;

    str<> tmp(install_key);
    lua_pushliteral(state, "exe");
    lua_pushstring(state, tmp.c_str());
    return 2;
}

//------------------------------------------------------------------------------
static int32 set_install_version(lua_State* state)
{
    const char* key = checkstring(state, 1);
    const char* ver = checkstring(state, 2);
    if (!key || !ver || _strnicmp(key, "clink_", 6))
        return 0;

    if (ver[0] == 'v')
        ver++;

    wstr<> where(c_uninstall_key);
    wstr<> wkey(key);
    where << L"\\" << wkey.c_str();

    HKEY hkey;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey);
    if (status)
        return 0;

    wstr<> name;
    wstr<> version(ver);
    name << L"Clink v" << version.c_str();

    bool ok = true;
    ok = ok && !RegSetValueExW(hkey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()), (name.length() + 1) * sizeof(*name.c_str()));
    ok = ok && !RegSetValueExW(hkey, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()), (version.length() + 1) * sizeof(*version.c_str()));
    RegCloseKey(hkey);

    if (!ok)
        return 0;

    lua_pushboolean(state, true);
    return 1;
}

//------------------------------------------------------------------------------
static int32 is_skip_update(lua_State* state)
{
    const char* new_ver = checkstring(state, 1);
    if (!new_ver)
        return 0;

    str<16> reg_ver;
    if (!is_update_skipped(new_ver, &reg_ver))
        return 0;

    lua_pushboolean(state, true);
    lua_pushlstring(state, reg_ver.c_str(), reg_ver.length());
    return 2;
}

//------------------------------------------------------------------------------
static int32 is_snoozed_update(lua_State* state)
{
    lua_pushboolean(state, is_update_prompt_snoozed());
    return 1;
}

//------------------------------------------------------------------------------
static int32 reset_update_keys(lua_State* state)
{
    wstr<> keyname;
    wstr<> valname;

    static const char* const c_subkeys[] =
    {
        "SnoozeUpdatePrompt",
        "SkipUpdate",
    };

    for (const auto subkey : c_subkeys)
    {
        if (make_key_value_names(subkey, keyname, valname))
        {
            HKEY hkey;
            LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, keyname.c_str(), 0, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, &hkey);
            if (status == ERROR_SUCCESS)
            {
                status = RegDeleteValueW(hkey, valname.c_str());
                RegCloseKey(hkey);
            }
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
static bool view_releases_page(HWND hdlg, uint32 index)
{
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecute(hdlg, nullptr, "https://github.com/chrisant996/clink/releases", 0, 0, SW_NORMAL);
    return false;
}

//------------------------------------------------------------------------------
static int32 show_update_prompt(lua_State* state)
{
    const char* ver = checkstring(state, 1);
    if (!ver || !*ver)
        return 0;

#ifdef DEBUG
    if (!lua_toboolean(state, 2))
    {
#endif

    assert(s_hMutex);
    if (!s_hMutex)
        return 0;

#ifdef DEBUG
    }
#endif

    if (is_update_prompt_snoozed() || is_update_skipped(ver))
    {
        lua_pushlstring(state, "ignore", 6);
        return 1;
    }

    str<> msg;
    str<> btn1;
    str<> btn2;
    msg.format("Clink %s is available.  What would you like to do?", ver);
    btn1.format("Install the %s update now.", ver);
    btn2.format("Skip the %s update.", ver);

    enum { btn_cancel, btn_install, btn_skip, btn_later, btn_notes };

    command_link_dialog dlg;
    dlg.add(btn_install, "&Update now", btn1.c_str());
    dlg.add(btn_skip, "&Skip this update", btn2.c_str());
    dlg.add(btn_later, "Wait until &later", "Do nothing now, but ask again later.");
    dlg.add(btn_notes, "View &Releases Page", "https://github.com/chrisant996/clink/releases", view_releases_page);

    const char* ret = "cancel";
    switch (dlg.do_modal(0, 180, "Clink Update", msg.c_str()))
    {
    case btn_install:
        ret = "update";
        break;
    case btn_skip:
        ret = "skip";
        skip_update(ver);
        break;
    case btn_later:
        ret = "later";
        snooze_update_prompt();
        break;
    }

    lua_pushstring(state, ret);
    return 1;
}



//------------------------------------------------------------------------------
static int32 _unzip_internal(lua_State* state)
{
    const bool ismain = is_main_coroutine(state);
    const char* const zip = checkstring(state, 1);
    const char* const dest = checkstring(state, 2);
    if (!zip || !*zip || !dest || !*dest)
        return 0;

    static uint32 s_counter = 0;
    str_moveable key;
    key.format("asyncunzip||%08x", ++s_counter);

    str<> src;
    get_lua_srcinfo(state, src);

    dbg_ignore_scope(snapshot, "async unzip");

    // Push an asyncyield object.
    async_yield_lua* asyncyield = nullptr;
    if (ismain)
        lua_pushnil(state);
    else if ((asyncyield = async_yield_lua::make_new(state, "unzip_internal")) == nullptr)
        return 0;

    // Push a task object.
    auto task = std::make_shared<unzip_async_lua_task>(key.c_str(), src.c_str(), asyncyield, zip, dest);
    if (!task)
        return 0;
    unzip_lua* request = unzip_lua::make_new(state, task);
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
static int32 async_path_type(lua_State* state)
{
    const char* path = checkstring(state, 1);
    int32 timeout = optinteger(state, 2, 0);
    if (!path || !*path)
        return 0;

    str<280> full;
    os::get_full_path_name(path, full);

    int32 type;
    if (!get_cached_path_type(full.c_str(), type))
    {
        str_moveable key;
        key.format("async||%s", full.c_str());
        std::shared_ptr<async_lua_task> task = find_async_lua_task(key.c_str());
        bool created = !task;
        if (!task)
        {
            str<> src;
            get_lua_srcinfo(state, src);

            task = std::make_shared<path_type_async_lua_task>(key.c_str(), src.c_str(), full.c_str());
            if (task && lua_isfunction(state, 3))
            {
                dbg_ignore_scope(snapshot, "async path type");
                lua_pushvalue(state, 3);
                int32 ref = luaL_ref(state, LUA_REGISTRYINDEX);
                task->set_callback(std::make_shared<callback_ref>(ref));
            }

            add_async_lua_task(task);
        }

        if (timeout)
            WaitForSingleObject(task->get_wait_handle(), timeout);

        if (!task->is_complete())
            return 0;

        std::shared_ptr<path_type_async_lua_task> pt_task = std::dynamic_pointer_cast<path_type_async_lua_task>(task);
        if (!pt_task)
            return 0;

        pt_task->disable_callback();

        type = pt_task->get_path_type();
    }

    const char* ret = nullptr;
    switch (type)
    {
    case os::path_type_file:    ret = "file"; break;
    case os::path_type_dir:     ret = "dir"; break;
    default:                    return 0;
    }

    lua_pushstring(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
extern int32 g_prompt_refilter;
int32 g_prompt_redisplay = 0;
static int32 get_refilter_redisplay_count(lua_State* state)
{
    lua_pushinteger(state, g_prompt_refilter);
    lua_pushinteger(state, g_prompt_redisplay);
    return 2;
}

//------------------------------------------------------------------------------
static int32 is_transient_prompt_filter(lua_State* state)
{
    lua_pushboolean(state, prompt_filter::is_transient_filtering());
    return 1;
}

//------------------------------------------------------------------------------
static int32 kick_idle(lua_State* state)
{
    extern void kick_idle();
    kick_idle();
    return 0;
}

//------------------------------------------------------------------------------
static int32 recognize_command(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const char* word = checkstring(state, 2);
    const bool quoted = lua_toboolean(state, 3);
    if (!line || !word)
        return 0;
    if (!*line || !*word)
        return 0;

    bool ready;
    const recognition recognized = recognize_command(line, word, quoted, ready, nullptr/*file*/);
    lua_pushinteger(state, int32(recognized));
    return 1;
}

//------------------------------------------------------------------------------
static int32 mark_deprecated_argmatcher(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (name)
        mark_deprecated_argmatcher(name);
    return 0;
}

//------------------------------------------------------------------------------
static int32 signal_delayed_init(lua_State* state)
{
    lua_input_idle::signal_delayed_init();
    return 0;
}

//------------------------------------------------------------------------------
static int32 signal_reclassify_line(lua_State* state)
{
    reclassify(reclassify_reason::lazy_force);
    return 0;
}

//------------------------------------------------------------------------------
static int32 get_cmd_commands(lua_State* state)
{
    lua_createtable(state, 64, 0);

    const char* const* const lists[] =
    {
        c_cmd_exes,
        c_cmd_commands_basicwordbreaks,
        c_cmd_commands_shellwordbreaks,
    };

    uint32 i = 0;
    for (const auto list : lists)
    {
        for (const char* const* cmd = list; *cmd; ++cmd)
        {
            lua_pushinteger(state, ++i);
            lua_pushstring(state, *cmd);
            lua_rawset(state, -3);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
static int32 is_cmd_command(lua_State* state)
{
    const char* word = checkstring(state, 1);
    if (!word)
        return 0;

    lua_pushboolean(state, is_cmd_command(word));
    return 1;
}

//------------------------------------------------------------------------------
static int32 is_cmd_wordbreak(lua_State* state)
{
    line_state_lua* lsl = line_state_lua::check(state, 1);
    if (!lsl)
        return 0;

    const line_state* line_state = lsl->get_line_state();
    const uint32 cwi = line_state->get_command_word_index();
    const word& info = line_state->get_words()[cwi];
    const char* line = line_state->get_line();

    str<> word;
    word.concat(line + info.offset, info.length);

    const state_flag flag = is_cmd_command(word.c_str());
    bool cmd_wordbreak = !!(flag & state_flag::flag_specialwordbreaks);

    if (!cmd_wordbreak)
    {
        bool ready;
        str<> file;
        const recognition recognized = recognize_command(nullptr, word.c_str(), info.quoted, ready, &file);
        if (ready)
        {
            const char* ext = path::get_extension(file.c_str());
            if (ext)
            {
                cmd_wordbreak = (_strcmpi(ext, ".bat") == 0 ||
                                 _strcmpi(ext, ".cmd") == 0);
            }
        }
    }

    lua_pushboolean(state, cmd_wordbreak);
    return 1;
}

//------------------------------------------------------------------------------
static int32 find_match_highlight(lua_State* state)
{
    const char* const match = checkstring(state, 1);
    const char* const _typed = checkstring(state, 2);
    if (!match || !_typed)
        return 0;
    if (!*match || !*_typed)
        return 0;

    str<> tmp;
    const char* typed = _typed;

again:
    int32 best_offset = -1;
    int32 best_length = -1;
    int32 typed_length = -1;
    for (const char* walk = match; *walk; ++walk)
    {
        int32 result = str_compare(walk, typed);
        if (result < 0)
        {
            best_offset = uint32(walk - match);
            best_length = (typed_length < 0) ? str_len(typed) : typed_length;
            break;
        }
        else if (result > best_length)
        {
            best_offset = uint32(walk - match);
            best_length = result;
            if (typed_length < 0)
                typed_length = str_len(typed);
            if (best_length >= typed_length)
                break;
        }
    }

    if (best_offset < 0 || best_length <= 0)
    {
        // If no match found and match.wild is enabled and typed has * or ?
        // then assume they are wildcards:  find the first non-wildcard
        // segment in the typed string and try searching for that.
        //
        // Why a post-processing approximation of the actual filename matching
        // that the match pipeline performs?  Because it's not worth the cost
        // of implementing something more pedantically precise.  And anyway,
        // technically wildcard matches always actually match the _entire_
        // string, so running a wildcard comparison would simply highlight the
        // entire match.  This is simpler and faster and yields more desirable
        // results except maybe in obscure edge cases.
        if (g_match_wild.get() && !tmp.length() && strpbrk(typed, "*?"))
        {
            const char* walk = _typed;
            while (*walk == '*' || *walk == '?')
                ++walk;
            while (*walk && *walk != '*' && *walk != '?')
            {
                tmp.concat(walk, 1);
                ++walk;
            }
            if (tmp.length())
            {
                typed = tmp.c_str();
                goto again;
            }
        }
        return 0;
    }

    lua_pushinteger(state, best_offset);
    lua_pushinteger(state, best_length);
    return 2;
}

//------------------------------------------------------------------------------
static int32 save_global_modes(lua_State* state)
{
    bool new_coroutine = lua_toboolean(state, 1);
    uint32 modes = lua_state::save_global_states(new_coroutine);
    lua_pushinteger(state, modes);
    return 1;
}

//------------------------------------------------------------------------------
static int32 restore_global_modes(lua_State* state)
{
#ifdef DEBUG
    const auto modes = checkinteger(state, 1);
    if (!modes.isnum())
        return 0;
#else
    uint32 modes = optinteger(state, 1, 0);
#endif
    lua_state::restore_global_states(modes);
    return 0;
}

//------------------------------------------------------------------------------
static int32 expand_prompt_codes(lua_State* state)
{
    const char* in = checkstring(state, 1);
    const bool rprompt = lua_toboolean(state, 2);
    if (!in)
        return 0;

    str<> out;
    const expand_prompt_flags flags = rprompt ? expand_prompt_flags::single_line : expand_prompt_flags::none;
    prompt_utils::expand_prompt_codes(in, out, flags);

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
static int32 _make_ftsc(lua_State* state)
{
    const char* code = checkstring(state, 1);
    if (!code)
        return 0;

    str<> s;
    make_ftsc(code, s);

    lua_pushlstring(state, s.c_str(), s.length());
    return 1;
}

//------------------------------------------------------------------------------
static int32 get_scripts_path(lua_State* state)
{
    int32 id;
    host_context context;
    host_get_app_context(id, context);

    lua_pushstring(state, context.scripts.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int32 is_break_on_error(lua_State* state)
{
    extern bool g_force_break_on_error;
    lua_pushboolean(state, g_force_break_on_error || g_lua_breakonerror.get());
    return 1;
}



//------------------------------------------------------------------------------
int32 make_dir_globber(lua_State* state)
{
    return globber_impl(state, true);
}

//------------------------------------------------------------------------------
int32 make_file_globber(lua_State* state)
{
    return globber_impl(state, false);
}

//------------------------------------------------------------------------------
int32 has_file_association(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (!name)
        return 0;

    const char* ext = path::get_extension(name);
    if (ext)
    {
        wstr<32> wext(ext);
        DWORD cchOut = 0;
        HRESULT hr = AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN|ASSOCF_NOFIXUPS, ASSOCSTR_FRIENDLYAPPNAME, wext.c_str(), nullptr, nullptr, &cchOut);
        if (FAILED(hr) || !cchOut)
            ext = nullptr;
    }

    lua_pushboolean(state, !!ext);
    return 1;
}

//------------------------------------------------------------------------------
int32 win_verify_trust(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (!name)
        return 0;

    str_moveable msg;
    if (win_verify_trust_file(name, msg))
    {
        lua_pushboolean(state, true);
        return 1;
    }
    else
    {
        lua_pushnil(state);
        lua_pushlstring(state, msg.c_str(), msg.length());
        return 2;
    }
}

//------------------------------------------------------------------------------
int32 verify_from_catalog(lua_State* state)
{
    const char* name = checkstring(state, 1);
    const char* catalog = checkstring(state, 2);
    if (!name || !catalog)
        return 0;

    str_moveable msg;
    if (win_verify_trust_catalog(catalog, name, msg))
    {
        lua_pushboolean(state, true);
        return 1;
    }
    else
    {
        lua_pushnil(state);
        lua_pushlstring(state, msg.c_str(), msg.length());
        return 2;
    }
}



//------------------------------------------------------------------------------
void internal_lua_initialise(lua_state& lua, bool lua_interpreter)
{
    struct method_def {
        bool        always;
        const char* name;
        int32       (*method)(lua_State*);
    };

    static const method_def methods[] = {
        // Formerly from the "clink." namespace ------------------------------

        // Suggestions
        { 0,    "history_suggester",        &history_suggester },
        { 0,    "set_suggestion_started",   &set_suggestion_started },
        { 0,    "set_suggestion_result",    &set_suggestion_result },
        { 0,    "_is_suggestionlist_mode",  &is_suggestionlist_mode },

        // Updater
        { 0,    "_acquire_updater_mutex",   &acquire_updater_mutex },
        { 0,    "_release_updater_mutex",   &release_updater_mutex },
        { 0,    "_get_installation_type",   &get_installation_type },
        { 0,    "_set_install_version",     &set_install_version },
        { 0,    "_is_skip_update",          &is_skip_update },
        { 0,    "_is_snoozed_update",       &is_snoozed_update },
        { 0,    "_reset_update_keys",       &reset_update_keys },
        { 0,    "_show_update_prompt",      &show_update_prompt },

        // Unzip
        { 1,    "_unzip_internal",          &_unzip_internal },

        { 0,    "_async_path_type",         &async_path_type },
        { 0,    "get_refilter_redisplay_count", &get_refilter_redisplay_count },
        { 0,    "istransientpromptfilter",  &is_transient_prompt_filter },
        { 0,    "kick_idle",                &kick_idle },
        { 0,    "_recognize_command",       &recognize_command },
        { 0,    "_mark_deprecated_argmatcher", &mark_deprecated_argmatcher },
        { 0,    "_signal_delayed_init",     &signal_delayed_init },
        { 0,    "_signal_reclassifyline",   &signal_reclassify_line },
        { 0,    "_get_cmd_commands",        &get_cmd_commands },
        { 0,    "is_cmd_command",           &is_cmd_command },
        { 0,    "is_cmd_wordbreak",         &is_cmd_wordbreak },
        { 0,    "_find_match_highlight",    &find_match_highlight },
        { 0,    "_save_global_modes",       &save_global_modes },
        { 0,    "_restore_global_modes",    &restore_global_modes },
        { 0,    "_expand_prompt_codes",     &expand_prompt_codes },
        { 0,    "_make_ftsc",               &_make_ftsc },
        { 0,    "_get_scripts_path",        &get_scripts_path },
        { 1,    "_is_break_on_error",       &is_break_on_error },

        // Formerly from the "os." namespace ---------------------------------

        { 1,    "_globdirs",                &api_glob_dirs },  // Public os.globdirs method is in core.lua.
        { 1,    "_globfiles",               &api_glob_files }, // Public os.globfiles method is in core.lua.
        { 1,    "_makedirglobber",          &make_dir_globber },
        { 1,    "_makefileglobber",         &make_file_globber },
        { 1,    "_hasfileassociation",      &has_file_association },
        { 1,    "_win_verify_trust",        &win_verify_trust },
        { 1,    "_verify_from_catalog",     &verify_from_catalog },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, 0, sizeof_array(methods));

    for (const auto& method : methods)
    {
        if (method.always || !lua_interpreter)
        {
            lua_pushstring(state, method.name);
            lua_pushcfunction(state, method.method);
            lua_rawset(state, -3);
        }
    }

    lua_setglobal(state, "import_internal");
}
