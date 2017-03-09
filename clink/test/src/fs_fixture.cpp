// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "fs_fixture.h"

#include <core/base.h>
#include <core/globber.h>
#include <core/os.h>
#include <core/path.h>

//------------------------------------------------------------------------------
static const char* g_default_fs[] = {
    "file1",
    "file2",
    "case_map-1",
    "case_map_2",
    "dir1/only",
    "dir1/file1",
    "dir1/file2",
    "dir2/.",
    nullptr,
};

//------------------------------------------------------------------------------
fs_fixture::fs_fixture(const char** fs)
{
    str<64> id;
    id.format("clink_test_%d", rand());

    os::get_env("tmp", m_root);
    path::append(m_root, id.c_str());

    bool existed = !os::make_dir(m_root.c_str());
    os::set_current_dir(m_root.c_str());

    if (existed)
        clean(m_root.c_str());

    if (fs == nullptr)
        fs = g_default_fs;

    const char** item = fs;
    while (const char* file = *item++)
    {
        str<> dir;
        path::get_directory(file, dir);
        os::make_dir(dir.c_str());

        if (FILE* f = fopen(file, "wt"))
            fclose(f);
    }

    m_fs = fs;
}

//------------------------------------------------------------------------------
fs_fixture::~fs_fixture()
{
    os::set_current_dir(m_root.c_str());
    os::set_current_dir("..");

    clean(m_root.c_str());
}

//------------------------------------------------------------------------------
void fs_fixture::clean(const char* path)
{
    str<> file;
    path::join(path, "*", file);

    globber globber(file.c_str());
    globber.hidden(true);
    while (globber.next(file))
    {
        if (os::get_path_type(file.c_str()) == os::path_type_dir)
            clean(file.c_str());
        else
            REQUIRE(os::unlink(file.c_str()));
    }

    REQUIRE(os::remove_dir(path));
}

//------------------------------------------------------------------------------
const char* fs_fixture::get_root() const
{
    return m_root.c_str();
}
