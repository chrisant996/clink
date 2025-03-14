// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/embedded_scripts.h>

#if defined(CLINK_USE_EMBEDDED_SCRIPTS)
    void lua_load_script_impl(class lua_state&, const char*, const char*, int32);

    #define lua_load_script(state, module, name)                                \
        do {                                                                    \
            extern const uint8* const module##_##name##_lua_script;             \
            extern const int32 module##_##name##_lua_script_len;                \
            lua_load_script_impl(                                               \
                state,                                                          \
                "@~clink~/" #module "/" #name ".lua",                           \
                (char*)module##_##name##_lua_script,                            \
                module##_##name##_lua_script_len);                              \
        } while(0)
#else
    void lua_load_script_impl(class lua_state&, const char*, int32);

    #define lua_load_script(state, module, name)                                \
        do {                                                                    \
            extern const char* const module##_##name##_lua_file;                \
            lua_load_script_impl(state, module##_##name##_lua_file, 0);         \
        } while(0)
#endif // CLINK_USE_EMBEDDED_SCRIPTS
