// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#ifdef CLINK_EMBED_LUA_SCRIPTS
    //------------------------------------------------------------------------------
    void lua_load_script_impl(struct lua_State*, const char*);

    #define lua_load_script(state, module, name)                           \
        {                                                                  \
            extern const char* module##_##name##_lua_script;               \
            lua_load_script_impl(state, module##_##name##_lua_script);     \
        }
#else
    //------------------------------------------------------------------------------
    void lua_load_script_impl(struct lua_State*, const char*, const char*);

    #define lua_load_script(state, module, name)                           \
        {                                                                  \
           extern const char* module##_embed_path;                         \
           extern const char* module##_##name##_lua_file;                  \
            lua_load_script_impl(state,                                    \
                module##_embed_path,                                       \
                module##_##name##_lua_file                                 \
            );\
        }
#endif // CLINK_EMBED_LUA_SCRIPTS
