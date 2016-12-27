// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "app_context.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <process/process.h>

//------------------------------------------------------------------------------
static const char* g_state_env_format = "=clink_state_%d";



//------------------------------------------------------------------------------
app_context::desc::desc()
{
    state_dir[0] = '\0';
}



//-----------------------------------------------------------------------------
app_context::app_context(const desc& desc)
: m_desc(desc)
{
    str_base state_dir(m_desc.state_dir);

    // The environment variable 'clink_profile' overrides all other state
    // path mechanisms.
    if (state_dir.empty())
        os::get_env("clink_profile", state_dir);

    // Look for a state directory that's been inherited in our environment.
    if (state_dir.empty())
        load_from_env();

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

    path::clean(state_dir);
    path::abs_path(state_dir);
    os::make_dir(state_dir.c_str());

    store_to_env();
}

//------------------------------------------------------------------------------
bool app_context::load_from_env()
{
    // Look up through process chain to see if we've inherited a state directory
    // in our environment.

    str<32> clink_env_var;
    str_base state_dir(m_desc.state_dir);

    int pid = -1;
    while (pid = process(pid).get_parent_pid())
    {
        clink_env_var.format(g_state_env_format, pid);
        if (os::get_env(clink_env_var.c_str(), state_dir))
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------
void app_context::store_to_env()
{
    // Set an environment variable that can be used to find Clink's state dir.
    str<32> name;
    name.format(g_state_env_format, process().get_pid());
    os::set_env(name.c_str(), m_desc.state_dir);
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

    int flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;
    flags |= GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    HINSTANCE module;
    if (!GetModuleHandleEx(flags, "clink", &module))
        return;

    GetModuleFileName(module, out.data(), out.size());
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return;

    path::get_directory(out);
    return;
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
