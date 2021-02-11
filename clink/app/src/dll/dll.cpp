// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "host/host_cmd.h"
#include "utils/app_context.h"
#include "utils/seh_scope.h"
#include "version.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/log.h>
#include <core/os.h>
#include <core/path.h>
#include <core/settings.h>
#include <core/str.h>
#include <core/str_tokeniser.h>

//------------------------------------------------------------------------------
const char* g_clink_header =
    "Clink v" CLINK_VERSION_STR "\n"
    "Copyright (c) 2012-2018 Martin Ridgers\n"
    "Portions Copyright (c) 2020-2021 Christopher Antos\n"
    "http://github.com/chrisant996/clink\n"
    ;



//------------------------------------------------------------------------------
static host* g_host = nullptr;



//------------------------------------------------------------------------------
static void success()
{
    if (!app_context::get()->is_quiet())
        puts(g_clink_header);
}

//------------------------------------------------------------------------------
static void failed()
{
    str<280> buffer;
    app_context::get()->get_state_dir(buffer);
    fprintf(stderr, "Failed to load Clink.\nSee log for details (%s).\n", buffer.c_str());
}

//------------------------------------------------------------------------------
static bool get_host_name(str_base& out)
{
    str<MAX_PATH> buffer;
    if (GetModuleFileName(nullptr, buffer.data(), buffer.size()) == buffer.size())
        return false;

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return false;

    return path::get_name(buffer.c_str(), out);
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

    if (logger* logger = logger::get())
        delete logger;

    delete app_context::get();
}

//------------------------------------------------------------------------------
INT_PTR WINAPI initialise_clink(const app_context::desc& app_desc)
{
    seh_scope seh;

    auto* app_ctx = new app_context(app_desc);

    // Start a log file.
    if (app_ctx->is_logging_enabled())
    {
        str<256> log_path;
        app_ctx->get_log_path(log_path);
        new file_logger(log_path.c_str());
    }

    // What process is the DLL loaded into?
    str<64> host_name;
    if (!get_host_name(host_name))
        return false;

    SYSTEMTIME now;
    GetLocalTime(&now);
    LOG("---- %04u/%02u/%02u %02u:%02u:%02u.%03u -------------------------------------------------",
        now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    LOG("Host process is '%s'", host_name.c_str());

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
                (system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64" : "x86");
        }
#ifdef _WIN64
        LOG("Clink version %s (x64)", CLINK_VERSION_STR);
#else
        LOG("Clink version %s (x86)", CLINK_VERSION_STR);
#endif
#pragma warning(pop)
    }

    // Search for a supported host.
    struct {
        const char* name;
        host*       (*creator)();
    } hosts[] = {
        { "cmd.exe", []() -> host* { return new host_cmd(); } },
    };

    for (int i = 0; i < sizeof_array(hosts); ++i)
        if (stricmp(host_name.c_str(), hosts[i].name) == 0)
            if (g_host = (hosts[i].creator)())
                break;

    // Bail out if this isn't a supported host.
    if (g_host == nullptr)
    {
        LOG("Unknown host.");
        return false;
    }

    // Validate and initialise.
    if (!g_host->validate())
    {
        LOG("Host validation failed.");
        return false;
    }

    if (!g_host->initialise())
    {
        failed();
        return false;
    }

    atexit(shutdown_clink);

    success();
    return true;
}
