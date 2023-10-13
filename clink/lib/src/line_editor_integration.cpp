// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "line_editor_integration.h"

#include <core/str.h>
#include <core/str_transform.h>
#include <core/str_unordered_set.h>
#include <core/linear_allocator.h>
#include <core/os.h>
#include <core/debugheap.h>

//------------------------------------------------------------------------------
static str_unordered_set s_deprecated_argmatchers;
static linear_allocator s_deprecated_argmatchers_store(1024);

//------------------------------------------------------------------------------
void clear_deprecated_argmatchers()
{
    s_deprecated_argmatchers.clear();
    s_deprecated_argmatchers_store.reset();
}

//------------------------------------------------------------------------------
void mark_deprecated_argmatcher(const char* command)
{
    if (s_deprecated_argmatchers.find(command) == s_deprecated_argmatchers.end())
    {
        dbg_ignore_scope(snapshot, "deprecated argmatcher lookup");
        const char* store = s_deprecated_argmatchers_store.store(command);
        s_deprecated_argmatchers.insert(store);
    }
}

//------------------------------------------------------------------------------
bool has_deprecated_argmatcher(const char* command)
{
    wstr<32> in(command);
    wstr<32> out;
    str_transform(in.c_str(), in.length(), out, transform_mode::lower);
    str<32> name(out.c_str());
    return s_deprecated_argmatchers.find(name.c_str()) != s_deprecated_argmatchers.end();
}



//------------------------------------------------------------------------------
extern "C" const char* host_get_env(const char* name)
{
    static int32 rotate = 0;
    static str<> rotating_tmp[10];

    str<>& s = rotating_tmp[rotate];
    rotate = (rotate + 1) % sizeof_array(rotating_tmp);
    if (!os::get_env(name, s))
        return nullptr;
    return s.c_str();
}

