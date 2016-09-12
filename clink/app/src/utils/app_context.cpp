// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "app_context.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>

//------------------------------------------------------------------------------
static char g_clink_symbol;



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

    // Override the profile path by either the "clink_profile" environment
    // variable or the --profile argument.
    os::get_env("clink_profile", state_dir);

    // Still no state directory set? Derive one.
    if (state_dir.empty())
    {
        wstr<272> wstate_dir;
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
    if (!GetModuleHandleEx(flags, &g_clink_symbol, &module))
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
