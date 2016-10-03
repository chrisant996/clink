// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

void lua_load_script_impl(class lua_state&, const char*);

#if defined(CLINK_FINAL)
    #define lua_load_script(state, module, name)                                \
        {                                                                       \
            extern const char* module##_##name##_lua_script;                    \
            lua_load_script_impl(state, module##_##name##_lua_script);          \
        }
#else
    #define lua_load_script(state, module, name)                                \
        {                                                                       \
            extern const char* module##_##name##_lua_file;                      \
            lua_load_script_impl(state, module##_##name##_lua_file);            \
        }
#endif // CLINK_FINAL
