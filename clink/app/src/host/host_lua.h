// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/str.h>
#include <lua/lua_match_generator.h>
#include <lua/lua_word_classifier.h>
#include <lua/lua_state.h>
#include <functional>

//------------------------------------------------------------------------------
class host_lua
{
public:
                        host_lua();
                        operator lua_state& ();
                        operator match_generator& ();
                        operator word_classifier& ();
    void                load_scripts();
    bool                is_script_path_changed() const;

    bool                send_event(const char* event_name, int nargs=0);
    bool                send_event_cancelable(const char* event_name, int nargs=0);

private:
    bool                load_scripts(const char* paths);
    void                load_script(const char* path);
    lua_state           m_state;
    lua_match_generator m_generator;
    lua_word_classifier m_classifier;
    str<>               m_prev_script_path;
};
