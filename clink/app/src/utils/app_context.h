// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/singleton.h>
#include <core/str.h>

//------------------------------------------------------------------------------
class app_context
    : public singleton<const app_context>
{
public:
    struct desc
    {
                desc();
        int32   id = 0;             // 0 auto-detects id.
        bool    quiet = false;
        bool    log = true;
        bool    inherit_id = false; // Allow auto-detecting id from environment.
        bool    force = false;      // Skip host check (for testbed).
        bool    detours = false;    // Use Detours for hooking, instead of IAT.
        char    state_dir[510];     // = {}; (this crashes cl.exe v18.00.21005.1)
        char    script_path[510];   // = {}; (this crashes cl.exe v18.00.21005.1)
    };

                app_context(const desc& desc);
    int32       get_id() const;
    bool        is_logging_enabled() const;
    bool        is_quiet() const;
    bool        is_detours() const;
    void        get_binaries_dir(str_base& out) const;
    void        get_state_dir(str_base& out) const;
    void        get_autostart_command(str_base& out) const;
    void        get_log_path(str_base& out) const;
    void        get_default_settings_file(str_base& out) const;
    void        get_settings_path(str_base& out) const;
    void        get_history_path(str_base& out) const;
    void        get_script_path(str_base& out) const;
    void        get_script_path_readable(str_base& out) const;
    void        get_default_init_file(str_base& out) const;
    bool        get_host_name(str_base& out) const;
    bool        update_env() const;
    void        start_logger() const;

private:
    void        get_script_path(str_base& out, bool readable) const;
    void        get_default_file(const char* name, str_base& out) const;
    void        init_binaries_dir();
    desc        m_desc;
    str<288>    m_binaries;
    str<16>     m_host_name;
    mutable str_moveable m_validated;
};
