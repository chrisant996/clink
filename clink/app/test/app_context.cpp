// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "clatch.h"

#include "env_fixture.h"
#include "fs_fixture.h"

#include <core/os.h>
#include <core/path.h>
#include <process/process.h>
#include <utils/app_context.h>

//------------------------------------------------------------------------------
TEST_CASE("App context origin sharing")
{
    const char* empty_fs[] = { nullptr };
    fs_fixture fs(empty_fs);
    const char* env_desc[] = {
        "clink_profile", fs.get_root(),
        nullptr
    };
    env_fixture env(env_desc);

    str<> module;
    REQUIRE(process().get_file_name(module));
    wstr<> origin_path(module.c_str());
    origin_path << L".origin";

    HANDLE origin = CreateFileW(origin_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(origin != INVALID_HANDLE_VALUE);
    clatch::cleanup cleanup([&]() {
        if (origin != INVALID_HANDLE_VALUE)
            CloseHandle(origin);
        REQUIRE(DeleteFileW(origin_path.c_str()));
    });

    str<> binaries;
    path::join(fs.get_root(), "installed clink", binaries);
    str<> dll;
    path::join(binaries.c_str(), "clink_dll_x64.dll", dll);
    DWORD written;
    REQUIRE(WriteFile(origin, dll.c_str(), dll.length(), &written, nullptr));
    REQUIRE(written == dll.length());
    CloseHandle(origin);
    origin = INVALID_HANDLE_VALUE;

    SECTION("Single reader")
    {
        app_context::desc desc;
        app_context context(desc);
        str<> actual;
        context.get_binaries_dir(actual);
        REQUIRE(actual.equals(binaries.c_str()));
    }

    SECTION("Concurrent readers")
    {
        origin = CreateFileW(origin_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        REQUIRE(origin != INVALID_HANDLE_VALUE);

        app_context::desc desc;
        app_context context(desc);
        str<> actual;
        context.get_binaries_dir(actual);
        REQUIRE(actual.equals(binaries.c_str()));
    }
}
