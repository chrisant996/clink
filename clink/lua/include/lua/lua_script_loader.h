// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/embedded_scripts.h>

void lua_load_script_impl(class lua_state&, const char*, int32);

#if defined(CLINK_USE_EMBEDDED_SCRIPTS)
    #define lua_load_script(state, module, name)                                \
        do {                                                                    \
            extern const uint8* module##_##name##_lua_script;                   \
            extern int32 module##_##name##_lua_script_len;                      \
            lua_load_script_impl(                                               \
                state,                                                          \
                (char*)module##_##name##_lua_script,                            \
                module##_##name##_lua_script_len);                              \
        } while(0)
#else
    #define lua_load_script(state, module, name)                                \
        do {                                                                    \
            extern const char* module##_##name##_lua_file;                      \
            lua_load_script_impl(state, module##_##name##_lua_file, 0);         \
        } while(0)
#endif // CLINK_USE_EMBEDDED_SCRIPTS
