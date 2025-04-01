// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"
#include "version.h"

#include <core/str.h>
#include <core/settings.h>
#include <core/os.h>
#include <core/path.h>
#include <getopt.h>

//------------------------------------------------------------------------------
static void print_info_line(HANDLE h, const char* s)
{
    DWORD dummy;
    if (GetConsoleMode(h, &dummy))
    {
        wstr<> tmp(s);
        DWORD written;
        WriteConsoleW(h, tmp.c_str(), tmp.length(), &written, nullptr);
    }
    else
    {
        printf("%s", s);
    }
}

//------------------------------------------------------------------------------
static void get_file_version(const WCHAR* file, str_base& version)
{
    char buffer[1024];
    VS_FIXEDFILEINFO* file_info;
    if (GetFileVersionInfoW(file, 0, sizeof(buffer), buffer) &&
        VerQueryValue(buffer, "\\", (void**)&file_info, nullptr))
    {
        version.format("%u.%u.%u",
                    HIWORD(file_info->dwFileVersionMS),
                    LOWORD(file_info->dwFileVersionMS),
                    HIWORD(file_info->dwFileVersionLS));
        return;
    }

    version = "version unknown";
}

//------------------------------------------------------------------------------
static bool is_injected(str_base& module, str_base& version)
{
    module.clear();
    version.clear();

    str<16> env_id;
    if (!os::get_env("=clink.id", env_id))
        return false;

    const int32 pid = atoi(env_id.c_str());
    if (!pid || pid == GetCurrentProcessId())
        return false;

    HANDLE th32 = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (th32 == INVALID_HANDLE_VALUE)
        return false;

    bool injected = false;

    MODULEENTRY32W module_entry = { sizeof(module_entry) };
    BOOL ok = Module32FirstW(th32, &module_entry);
    while (ok)
    {
        if (_wcsnicmp(module_entry.szModule, L"clink_dll_", 10) == 0)
        {
            injected = true;

            get_file_version(module_entry.szExePath, version);

            const uint32 trunc_len = version.length();
            version.concat(".", 1);
            if (_strnicmp(CLINK_VERSION_STR, version.c_str(), version.length()) == 0)
            {
                module = module_entry.szModule;
                version.clear();
            }
            else
            {
                module = module_entry.szExePath;
                version.truncate(trunc_len);
            }
            break;
        }

        ok = Module32NextW(th32, &module_entry);
    }

    CloseHandle(th32);
    return injected;
}

//------------------------------------------------------------------------------
int32 clink_info(int32 argc, char** argv)
{
    static const struct {
        const char* name;
        void        (app_context::*method)(str_base&) const;
        bool        suppress_when_empty;
    } infos[] = {
        { "binaries",           &app_context::get_binaries_dir },
        { "state",              &app_context::get_state_dir },
        { "log",                &app_context::get_log_path },
        { "default settings",   &app_context::get_default_settings_file, true/*suppress_when_empty*/ },
        { "settings",           &app_context::get_settings_path },
        { "history",            &app_context::get_history_path },
        { "scripts",            &app_context::get_script_path_readable, true/*suppress_when_empty*/ },
        { "default_inputrc",    &app_context::get_default_init_file, true/*suppress_when_empty*/ },
    };

    struct info_output
    {
                        info_output(const char* name, str_moveable&& value) : name(name), value(std::move(value)) {}
        const char*     name;
        str_moveable    value;
    };

    const auto* context = app_context::get();

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    // Load the settings from disk, since script paths are affected by settings.
    str<280> settings_file;
    str<280> default_settings_file;
    context->get_settings_path(settings_file);
    context->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());

    // Get values to output.
    int32 spacing = 8;
    std::vector<info_output> outputs;
    for (const auto& info : infos)
    {
        str_moveable out;
        (context->*info.method)(out);
        if (!info.suppress_when_empty || !out.empty())
        {
            outputs.emplace_back(info.name, std::move(out));
            spacing = max<int32>(spacing, int32(strlen(info.name)));
        }
    }

    // Version information.
    printf("%-*s : %s\n", spacing, "version", CLINK_VERSION_STR_WITH_BRANCH);
#ifdef DEBUG
    printf("%-*s : %s\n", spacing, "flavor", "DEBUG");
#endif
    printf("%-*s : %d\n", spacing, "session", context->get_id());

    // Check whether injected.
    str<> dll;
    str<> version;
    if (is_injected(dll, version))
    {
        if (version.empty())
            printf("%-*s : %s\n", spacing, "injected", dll.c_str());
        else
            printf("%-*s : %s (%s)\n", spacing, "injected", dll.c_str(), version.c_str());
    }

    // Output the values.
    str<> s;
    for (const auto& output : outputs)
    {
        s.clear();
        s.format("%-*s : %s\n", spacing, output.name, output.value.c_str());
        print_info_line(h, s.c_str());
    }

    // Inputrc environment variables.
    static const char* const env_vars[] = {
        "clink_inputrc",
        "", // Magic value handled specially below.
        "userprofile",
        "localappdata",
        "appdata",
        "home"
    };

    // Inputrc file names.
    static const char *const file_names[] = {
        ".inputrc",
        "_inputrc",
        "clink_inputrc",
    };

    bool labeled = false;
    bool first = true;
    for (const char* env_var : env_vars)
    {
        bool use_state_dir = !*env_var;
        const char* label = labeled ? "" : "inputrc";
        labeled = true;
        if (use_state_dir)
            printf("%-*s : %s\n", spacing, label, "state directory");
        else
            printf("%-*s : %%%s%%\n", spacing, label, env_var);

        str<280> out;
        if (use_state_dir)
        {
            context->get_state_dir(out);
        }
        else if (!os::get_env(env_var, out))
        {
            printf("%-*s     (unset)\n", spacing, "");
            continue;
        }

        int32 base_len = out.length();

        for (int32 i = 0; i < sizeof_array(file_names); ++i)
        {
            out.truncate(base_len);
            path::append(out, file_names[i]);

            bool exists = os::get_path_type(out.c_str()) == os::path_type_file;

            const char* status;
            if (!exists)
                status = "";
            else if (first)
                status = "   (LOAD)";
            else
                status = "   (exists)";

            if (exists || i < 2)
            {
                s.clear();
                s.format("%-*s     %s%s\n", spacing, "", out.c_str(), status);
                print_info_line(h, s.c_str());
            }

            if (exists)
                first = false;
        }
    }

    os::make_version_string(s);
    printf("%-*s : %s\n", spacing, "system", s.c_str());

    const DWORD cpid = GetACP();
    const DWORD kbid = LOWORD(GetKeyboardLayout(0));
    WCHAR wide_layout_name[KL_NAMELENGTH * 2];
    if (!GetKeyboardLayoutNameW(wide_layout_name))
        wide_layout_name[0] = 0;
    str<> layout_name(wide_layout_name);

    printf("%-*s : %u\n", spacing, "codepage", cpid);
    printf("%-*s : %u\n", spacing, "keyboard langid", kbid);
    s.clear();
    s.format("%-*s : %s\n", spacing, "keyboard layout", layout_name.c_str());
    print_info_line(h, s.c_str());

    str<> state_dir;
    context->get_state_dir(state_dir);
    if (os::get_path_type(state_dir.c_str()) == os::path_type_file)
    {
        fprintf(stderr,
                "warning: invalid profile directory '%s'.\n"
                "The profile directory must be a directory, but it is a file.\n",
                state_dir.c_str());
    }

    return 0;
}
