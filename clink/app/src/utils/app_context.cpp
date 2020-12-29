// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "app_context.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/settings.h>
#include <process/process.h>
#include <process/vm.h>

//------------------------------------------------------------------------------
static setting_str g_clink_path(
    "clink.path",
    "Paths to load Lua completion scripts from",
    "These paths will be searched for Lua scripts that provide custom\n"
    "match generation. Multiple paths should be delimited by semicolons.",
    "");



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
}

//------------------------------------------------------------------------------
void app_context::get_script_path(str_base& out, bool readable) const
{
    str<280> tmp;

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

    // Finally load from the clink_path envvar.
    if (os::get_env("clink_path", tmp) && tmp.length())
    {
        if (out.length())
            out << (readable ? " ; " : ";");
        out << tmp.c_str();
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
