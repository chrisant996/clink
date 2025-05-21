// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "app_context.h"
#include "version.h"

#include <core/base.h>
#include <core/os.h>
#include <core/path.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/settings.h>
#include <core/log.h>
#include <process/process.h>
#include <process/vm.h>

//------------------------------------------------------------------------------
static setting_str g_clink_path(
    "clink.path",
    "Paths to load Lua scripts from",
    "These paths will be searched for Lua scripts that provide custom\n"
    "match generation, prompt filtering, and etc.  Multiple paths should be\n"
    "delimited by semicolons.  Setting this loads scripts from here INSTEAD of\n"
    "from the Clink binaries directory and profile directory.",
    "");

static setting_str g_clink_autostart(
    "clink.autostart",
    "Command to run when injected",
    "This command is automatically run when the first CMD prompt is shown after\n"
    "Clink is injected.  If this is blank (the default), then Clink instead looks\n"
    "for clink_start.cmd in the binaries directory and profile directory and runs\n"
    "them.  Set it to \"nul\" to not run any autostart command.",
    "");



//------------------------------------------------------------------------------
#ifdef DEBUG
wait_for_attach_mode get_wait_for_attach()
{
    DWORD type;
    DWORD data;
    DWORD size = sizeof(data);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Clink", L"WaitForAttach", RRF_RT_REG_DWORD, &type, &data, &size);
    if (status == ERROR_SUCCESS && type == REG_DWORD)
    {
        switch (data)
        {
        case 1: return wait_for_attach_mode::WAIT;
        case 2: return wait_for_attach_mode::CTRL500;
        }
    }
    return wait_for_attach_mode::NONE;
}
#endif

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

    if (desc.force)
    {
        m_host_name = "cmd.exe";
    }
    else
    {
        char buffer[280];
        DWORD len = GetModuleFileName(nullptr, buffer, sizeof_array(buffer));
        if (len && len < sizeof_array(buffer))
            path::get_name(buffer, m_host_name);
    }

    // The environment variable 'clink_profile' overrides all other state
    // path mechanisms.
    {
        str<> env;
        if (os::get_env("clink_profile", env))
            path::tilde_expand(env.c_str(), state_dir, true/*use_appdata_local*/);
    }

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

    if (m_desc.id == 0)
    {
        m_desc.id = process().get_pid();
        if (desc.inherit_id)
        {
            str<16, false> env_id;
            if (os::get_env("=clink.id", env_id))
                m_desc.id = atoi(env_id.c_str());
        }
    }

    init_binaries_dir();

    update_env();
}

//-----------------------------------------------------------------------------
int32 app_context::get_id() const
{
    return m_desc.id;
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
bool app_context::is_detours() const
{
    return m_desc.detours;
}

//------------------------------------------------------------------------------
void app_context::get_binaries_dir(str_base& out) const
{
    out = m_binaries.c_str();
}

//------------------------------------------------------------------------------
void app_context::get_state_dir(str_base& out) const
{
    out.copy(m_desc.state_dir);
}

//------------------------------------------------------------------------------
void app_context::get_autostart_command(str_base& out) const
{
    g_clink_autostart.get(out);
    if (out.empty())
    {
        str<> file;

        str<> binaries_dir;
        get_binaries_dir(binaries_dir);
        if (path::join(binaries_dir.c_str(), "clink_start.cmd", file))
        {
            if (os::get_path_type(file.c_str()) == os::path_type_file)
                out << (out.length() ? " & " : "") << "\"" << file.c_str() << "\"";
        }

        str<> state_dir;
        get_state_dir(state_dir);
        if (path::join(state_dir.c_str(), "clink_start.cmd", file) &&
            !binaries_dir.iequals(state_dir.c_str()))
        {
            if (os::get_path_type(file.c_str()) == os::path_type_file)
                out << (out.length() ? " & " : "") << "\"" << file.c_str() << "\"";
        }
    }
    else if (out.iequals("nul") || out.iequals("\"nul\""))
    {
        out.clear();
    }
}

//------------------------------------------------------------------------------
void app_context::get_log_path(str_base& out) const
{
    get_state_dir(out);
    path::append(out, "clink.log");
}

//------------------------------------------------------------------------------
void app_context::get_default_settings_file(str_base& out) const
{
    get_default_file("default_settings", out);
}

//------------------------------------------------------------------------------
void app_context::get_settings_path(str_base& out) const
{
    if (!os::get_env("clink_settings", out) || out.empty())
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
    if (!os::get_env("clink_history_label", label))
        return;

    label.trim();

    uint32 label_len = 0;
    str_iter iter(label);
    while (iter.more())
    {
        const char* ptr = iter.get_pointer();
        uint32 c = iter.next();
        if (iswalnum(c))
        {
            if (!label_len)
                out.concat("-", 1);
            out.concat(ptr, uint32(iter.get_pointer() - ptr));
            label_len++;
            if (label_len >= 32)
                break;
        }
    }
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
        get_binaries_dir(tmp);
        if (tmp.length())
        {
            if (out.length())
                out << (readable ? " ; " : ";");
            out << tmp.c_str();
        }

        get_state_dir(tmp);
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

//------------------------------------------------------------------------------
void app_context::get_default_init_file(str_base& out) const
{
    get_default_file("default_inputrc", out);
}

//------------------------------------------------------------------------------
bool app_context::get_host_name(str_base& out) const
{
    out = m_host_name.c_str();
    return !m_host_name.empty();
}

//------------------------------------------------------------------------------
void app_context::get_default_file(const char* name, str_base& out) const
{
    get_state_dir(out);
    path::append(out, name);
    if (os::get_path_type(out.c_str()) == os::path_type_file)
        return;

    get_binaries_dir(out);
    path::append(out, name);
    if (os::get_path_type(out.c_str()) == os::path_type_file)
        return;

    out.clear();
}

//-----------------------------------------------------------------------------
void app_context::init_binaries_dir()
{
    void* base = vm().get_alloc_base((void*)"");
    if (base == nullptr)
        return;

    wstr<280> wout;
    DWORD wout_len = GetModuleFileNameW(HMODULE(base), wout.data(), wout.size());
    if (!wout_len || wout_len >= wout.size())
        return;

    // Check for a .origin suffix indicating that we're using a copied DLL.
    int32 out_length = wout_len;
    wout << L".origin";
    HANDLE origin = CreateFileW(wout.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (origin != INVALID_HANDLE_VALUE)
    {
        DWORD read;
        int32 size = GetFileSize(origin, nullptr);
        m_binaries.reserve(size);
        ReadFile(origin, m_binaries.data(), size, &read, nullptr);
        CloseHandle(origin);
        m_binaries.truncate(size);
    }
    else
    {
        wout.truncate(out_length);
        m_binaries = wout.c_str();
    }

    path::get_directory(m_binaries);
}

//-----------------------------------------------------------------------------
bool app_context::update_env() const
{
    str<280> tmp;
    bool reset = false;

    // Check if a new scripts or profile has been injected.  This lets Cmder be
    // compatible with Clink auto-run by updating the scripts and profile paths
    // via a second `clink inject` even though Clink is already injected.
    os::get_env("=clink.scripts.inject", tmp);
    if (!tmp.empty())
    {
        str_base script_path(const_cast<char*>(m_desc.script_path), sizeof_array(m_desc.script_path));
        script_path.copy(tmp.c_str());
        os::set_env("=clink.scripts.inject", nullptr);
        reset = true;
    }
    os::get_env("=clink.profile.inject", tmp);
    if (!tmp.empty())
    {
        str_base state_dir(const_cast<char*>(m_desc.state_dir), sizeof_array(m_desc.state_dir));
        state_dir.copy(tmp.c_str());
        os::set_env("=clink.profile.inject", nullptr);
        start_logger(); // Restart the logger since the log file location has changed.
        reset = true;
    }

    str<48> id_str;
    id_str.format("%d", m_desc.id);
    os::set_env("=clink.id", id_str.c_str());
    os::set_env("=clink.profile", m_desc.state_dir);
    os::set_env("=clink.scripts", m_desc.script_path);

    str<280> bin_dir;
    get_binaries_dir(bin_dir);
    os::set_env("=clink.bin", bin_dir.c_str());

    if (!m_desc.inherit_id && !m_validated.equals(m_desc.state_dir))
    {
        if (os::get_path_type(m_desc.state_dir) == os::path_type_file)
        {
            fprintf(stderr,
                    "warning: invalid profile directory '%s'.\n"
                    "The profile directory must be a directory, but it is a file.\n",
                    m_desc.state_dir);
        }
        m_validated = m_desc.state_dir;
    }

    return reset;
}

//------------------------------------------------------------------------------
void app_context::start_logger() const
{
    if (is_logging_enabled())
    {
        // Discard any existing logger.  This is so Cmder can be compatible with
        // Clink autorun and still override the scripts and profile paths.
        if (logger::get())
            delete logger::get();

        str<256> log_path;
        get_log_path(log_path);
        unlink(log_path.c_str()); // Restart the log file on every inject.
        new file_logger(log_path.c_str());

        SYSTEMTIME now;
        GetLocalTime(&now);
        LOG("---- %04u/%02u/%02u %02u:%02u:%02u.%03u -------------------------------------------------",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        const char* host_name = m_host_name.empty() ? "<unknown>" : m_host_name.c_str();
        LOG("Host process is '%s' (pid %d)", host_name, get_id());

        str<> dll_path;
        get_binaries_dir(dll_path);
        LOG("DLL path is '%s'", dll_path.c_str());

        const void* const base = vm().get_alloc_base((void*)"");
        LOG("DLL base is %p", base);

        {
#pragma warning(push)
#pragma warning(disable:4996)
            OSVERSIONINFO ver = { sizeof(ver) };
            if (GetVersionEx(&ver))
            {
                SYSTEM_INFO system_info;
                GetNativeSystemInfo(&system_info);
                LOG("Windows version %u.%u.%u (%s)",
                    ver.dwMajorVersion,
                    ver.dwMinorVersion,
                    ver.dwBuildNumber,
                    (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64" :
                    ((system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? "arm64" : "x86"));
            }
            LOG("Clink version %s (%s)", CLINK_VERSION_STR_WITH_BRANCH, AS_STR(ARCHITECTURE_NAME));
#pragma warning(pop)
        }
    }
}
