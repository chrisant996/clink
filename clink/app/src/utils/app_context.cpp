// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "app_context.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/settings.h>
#include <process/process.h>
#include <process/vm.h>

//------------------------------------------------------------------------------
static setting_str g_clink_path(
    "clink.path",
    "Paths to load Lua scripts from",
    "These paths will be searched for Lua scripts that provide custom\n"
    "match generation, prompt filtering, and etc. Multiple paths should be\n"
    "delimited by semicolons. Setting this loads scripts from here INSTEAD of\n"
    "from the Clink binaries directory and config directory.",
    "");



//------------------------------------------------------------------------------
void get_installed_scripts(str_base& out)
{
    out.clear();

    DWORD type;
    DWORD size;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Clink", L"InstalledScripts", RRF_RT_REG_SZ, &type, nullptr, &size);
    if (status == ERROR_SUCCESS && type == REG_SZ)
    {
        WCHAR* buffer = static_cast<WCHAR*>(malloc(size + sizeof(*buffer)));
        if (buffer)
        {
            status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Clink", L"InstalledScripts", RRF_RT_REG_SZ, &type, buffer, &size);
            if (status == ERROR_SUCCESS && type == REG_SZ)
                to_utf8(out, buffer);
            free(buffer);
        }
    }
}

//------------------------------------------------------------------------------
bool set_installed_scripts(const char* in)
{
    wstr<> out;
    to_utf16(out, in);

    HKEY hkey;
    DWORD dwDisposition;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Clink", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, nullptr, &hkey, &dwDisposition);
    if (status != ERROR_SUCCESS)
        return false;

    status = RegSetValueExW(hkey, L"InstalledScripts", 0, REG_SZ, reinterpret_cast<const BYTE*>(out.c_str()), out.length() * sizeof(*out.c_str()));
    RegCloseKey(hkey);
    if (status != ERROR_SUCCESS)
        return false;

    return true;
}



//------------------------------------------------------------------------------
app_context::desc::desc()
{
    state_dir[0] = '\0';
    script_path[0] = '\0';
}



//-----------------------------------------------------------------------------
app_context::app_context(const desc& desc)
: m_desc(desc)
{
    str_base state_dir(m_desc.state_dir);
    str_base script_path(m_desc.script_path);

    // The environment variable 'clink_profile' overrides all other state
    // path mechanisms.
    if (state_dir.empty())
        os::get_env("clink_profile", state_dir);

    // Look for a state directory that's been inherited in our environment.
    if (state_dir.empty())
        os::get_env("=clink.profile", state_dir);

    // Look for a script directory that's been inherited in our environment.
    if (script_path.empty())
        os::get_env("=clink.scripts", script_path);

    // Still no state directory set? Derive one.
    if (state_dir.empty())
    {
        wstr<280> wstate_dir;
        if (SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA, nullptr, 0, wstate_dir.data()) == S_OK)
            state_dir = wstate_dir.c_str();
        else if (!os::get_env("userprofile", state_dir))
            os::get_temp_dir(state_dir);

        if (!state_dir.empty())
            path::append(state_dir, "clink");
    }

    path::normalise(state_dir);
    os::make_dir(state_dir.c_str());

    path::normalise(script_path);

    m_id = process().get_pid();
    if (desc.inherit_id)
    {
        str<16, false> env_id;
        if (os::get_env("=clink.id", env_id))
            m_id = atoi(env_id.c_str());
    }

    update_env();
}

//-----------------------------------------------------------------------------
int app_context::get_id() const
{
    return m_id;
}

//------------------------------------------------------------------------------
bool app_context::is_logging_enabled() const
{
    return m_desc.log;
}

//------------------------------------------------------------------------------
bool app_context::is_quiet() const
{
    return m_desc.quiet;
}

//------------------------------------------------------------------------------
void app_context::get_binaries_dir(str_base& out) const
{
    out.clear();

    void* base = vm().get_alloc_base("");
    if (base == nullptr)
        return;

    wstr<280> wout;
    GetModuleFileNameW(HMODULE(base), wout.data(), wout.size());
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return;

    // Check for a .origin suffix indicating that we're using a copied DLL.
    int out_length = wout.length();
    wout << L".origin";
    HANDLE origin = CreateFileW(wout.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (origin != INVALID_HANDLE_VALUE)
    {
        DWORD read;
        int size = GetFileSize(origin, nullptr);
        out.reserve(size + 1);
        ReadFile(origin, out.data(), size, &read, nullptr);
        out.data()[size] = '\0';
        CloseHandle(origin);
    }
    else
    {
        wout.truncate(out_length);
        out = wout.c_str();
    }

    path::get_directory(out);
}

//------------------------------------------------------------------------------
void app_context::get_state_dir(str_base& out) const
{
    out.copy(m_desc.state_dir);
}

//------------------------------------------------------------------------------
void app_context::get_log_path(str_base& out) const
{
    get_state_dir(out);
    path::append(out, "clink.log");
}

//------------------------------------------------------------------------------
void app_context::get_settings_path(str_base& out) const
{
    get_state_dir(out);
    path::append(out, "clink_settings");
}

//------------------------------------------------------------------------------
void app_context::get_history_path(str_base& out) const
{
    get_state_dir(out);
    path::append(out, "clink_history");

    // Optionally add a qualifying label of up to 32 alphanumeric characters.
    // This enables different instances to use different master history lists.
    str<64> label;
    if (os::get_env("clink_history_label", label))
    {
        unsigned int label_len = 0;
        str_iter iter(label);
        while (iter.more())
        {
            const char* ptr = iter.get_pointer();
            unsigned int c = iter.next();
            if (iswalnum(c))
            {
                if (!label_len)
                    out.concat("-", 1);
                out.concat(ptr, static_cast<unsigned int>(iter.get_pointer() - ptr));
                label_len++;
                if (label_len >= 32)
                    break;
            }
        }
    }
}

//------------------------------------------------------------------------------
void app_context::get_script_path(str_base& out, bool readable) const
{
    str<280> tmp;

    // Check if a new scripts path has been injected.  This is so Cmder can be
    // compatible with Clink auto-run by updating the scripts path via a second
    // `clink inject` even though Clink is already injected.
    os::get_env("=clink.scripts.inject", tmp);
    if (!tmp.empty())
    {
        str_base script_path(const_cast<char*>(m_desc.script_path), sizeof_array(m_desc.script_path));
        script_path.copy(tmp.c_str());
        os::set_env("=clink.scripts", tmp.c_str());
        os::set_env("=clink.scripts.inject", nullptr);
    }

    // The --scripts flag happens before anything else.
    out.clear();
    out << m_desc.script_path;

    // Next load from the clink.path setting, otherwise from the binary
    // directory and the profile directory.
    const char* setting_clink_path = g_clink_path.get();
    if (setting_clink_path && *setting_clink_path)
    {
        if (out.length())
            out << (readable ? " ; " : ";");
        out << setting_clink_path;
    }
    else
    {
        app_context::get()->get_binaries_dir(tmp);
        if (tmp.length())
        {
            if (out.length())
                out << (readable ? " ; " : ";");
            out << tmp.c_str();
        }

        app_context::get()->get_state_dir(tmp);
        if (tmp.length())
        {
            if (out.length())
                out << (readable ? " ; " : ";");
            out << tmp.c_str();
        }
    }

    // Next load from the clink_path envvar.
    if (os::get_env("clink_path", tmp) && tmp.length())
    {
        if (out.length())
            out << (readable ? " ; " : ";");
        out << tmp.c_str();
    }

    // Finally load from installed script paths.
    str<> installed_scripts;
    get_installed_scripts(installed_scripts);
    if (installed_scripts.length())
    {
        if (out.length())
            out << (readable ? " ; " : ";");
        out << installed_scripts;
    }
}

//------------------------------------------------------------------------------
void app_context::get_script_path(str_base& out) const
{
    return get_script_path(out, false);
}

//------------------------------------------------------------------------------
void app_context::get_script_path_readable(str_base& out) const
{
    return get_script_path(out, true);
}

//-----------------------------------------------------------------------------
void app_context::update_env() const
{
    str<48> id_str;
    id_str.format("%d", m_id);
    os::set_env("=clink.id", id_str.c_str());
    os::set_env("=clink.profile", m_desc.state_dir);
    os::set_env("=clink.scripts", m_desc.script_path);
}

//------------------------------------------------------------------------------
/*static*/ void app_context::override_id(int id)
{
    app_context* app = const_cast<app_context*>(get());
    app->m_id = id;
}
