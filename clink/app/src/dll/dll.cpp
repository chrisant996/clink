// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_cmd.h"
#include "utils/app_context.h"
#include "utils/seh_scope.h"
#include "utils/usage.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>
#include <lib/recognizer.h>
#include <lua/lua_task_manager.h>

//------------------------------------------------------------------------------
static host* g_host = nullptr;



//------------------------------------------------------------------------------
static void success()
{
    auto app = app_context::get();

    if (app->is_quiet())
        return;

    // Load settings to check if the logo should be abbreviated or omitted.
    str<288> settings_file;
    str<288> default_settings_file;
    app->get_settings_path(settings_file);
    app->get_default_settings_file(default_settings_file);
    settings::load(settings_file.c_str(), default_settings_file.c_str());
    maybe_print_logo();
}

//------------------------------------------------------------------------------
static void failed()
{
    auto app = app_context::get();

    str<280> buffer;
    app->get_state_dir(buffer);
    fprintf(stderr, "Failed to load Clink.\n");
    if (app->is_logging_enabled())
    {
        app->get_log_path(buffer);
        fprintf(stderr, "See log file for details (%s).\n", buffer.c_str());
    }
    else
    {
        fprintf(stderr, "Enable logging for details.\n");
    }
}

//------------------------------------------------------------------------------
static void shutdown_clink()
{
    seh_scope seh;

    if (g_host != nullptr)
    {
        g_host->shutdown();
        delete g_host;
        g_host = nullptr;
    }

    shutdown_task_manager(true/*final*/);
    shutdown_recognizer();

    if (logger* logger = logger::get())
        delete logger;

    delete app_context::get();
}

//------------------------------------------------------------------------------
INT_PTR WINAPI initialise_clink(const app_context::desc& app_desc)
{
    {
        static bool s_initialized = false;
        if (s_initialized)
        {
            // In this weird case, the logger doesn't need to be started,
            // because Clink is already injected and already started the logger.
            LOG("Clink is already installed in the target process.  An antivirus tool might be blocking Clink from inspecting the target process.");
            return false;
        }
        s_initialized = true;
    }

    install_crt_invalid_parameter_handler();

#ifdef DEBUG
    {
        DWORD type;
        DWORD data;
        DWORD size = sizeof(data);
        LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Clink", L"WaitForAttach", RRF_RT_REG_DWORD, &type, &data, &size);
        if (status == ERROR_SUCCESS && type == REG_DWORD)
        {
            bool wait = (data == 1);
            if (data == 2)
            {
                const DWORD began = GetTickCount();
                while (GetKeyState(VK_CONTROL) < 0)
                {
                    wait = (GetTickCount() - began > 500);
                    if (wait)
                        break;
                    Sleep(10);
                }
            }
            if (wait)
            {
                str<> msg;
                DWORD pid = GetCurrentProcessId();
                msg.format("Attach debugger to process %u (0x%x) and click OK.", pid, pid);
                MessageBox(0, msg.c_str(), "Clink", MB_OK);
            }
        }
    }
#endif

    // Now that Clink has a background thread, it gets trickier to accurately
    // attribute crashes to Clink.  The exception filter is per-process, so for
    // now install it permanently, and use thread local state to determine
    // whether Clink code crashes.  Since it's so rare to encounter a crash in
    // CMD code, replacing the exception filter for the whole process seems
    // unlikely to create problems.
    install_exception_filter();

    seh_scope seh;

    auto* app_ctx = new app_context(app_desc);

    app_ctx->start_logger();

    // What process is the DLL loaded into?
    str<64> host_name;
    if (!app_ctx->get_host_name(host_name))
    {
        ERR("Unable to get host name.");
        return false;
    }

    // Search for a supported host (keep in sync with inject_dll in inject.cpp).
    struct {
        const char* name;
        host*       (*creator)();
    } hosts[] = {
        { "cmd.exe", []() -> host* { return new host_cmd(); } },
    };

    for (int32 i = 0; i < sizeof_array(hosts); ++i)
        if (stricmp(host_name.c_str(), hosts[i].name) == 0)
            if (g_host = (hosts[i].creator)())
                break;

    // Bail out if this isn't a supported host.
    if (g_host == nullptr)
    {
        LOG("Unknown host '%s'.", host_name.c_str());
        return false;
    }

    // Validate and initialise.  Negative means an ignorable error that should
    // not be reported.
    int32 validate = g_host->validate();
    if (validate <= 0)
        return validate;

    if (!g_host->initialise())
    {
        failed();
        return false;
    }

    atexit(shutdown_clink);

    success();
    return true;
}
