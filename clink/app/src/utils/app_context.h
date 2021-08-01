// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/singleton.h>

class str_base;

//------------------------------------------------------------------------------
class app_context
    : public singleton<const app_context>
{
public:
    struct desc
    {
                desc();
        int     id = 0;             // 0 auto-detects id.
        bool    quiet = false;
        bool    log = true;
        bool    inherit_id = false; // Allow auto-detecting id from environment.
        bool    force = false;      // Skip host check (for testbed).
        char    unused = 0;
        char    state_dir[510];     // = {}; (this crashes cl.exe v18.00.21005.1)
        char    script_path[510];   // = {}; (this crashes cl.exe v18.00.21005.1)
    };

                app_context(const desc& desc);
    int         get_id() const;
    bool        is_logging_enabled() const;
    bool        is_quiet() const;
    void        get_binaries_dir(str_base& out) const;
    void        get_state_dir(str_base& out) const;
    void        get_autostart_command(str_base& out) const;
    void        get_log_path(str_base& out) const;
    void        get_settings_path(str_base& out) const;
    void        get_history_path(str_base& out) const;
    void        get_script_path(str_base& out) const;
    void        get_script_path_readable(str_base& out) const;
    bool        update_env() const;

    static void override_id(int id);

private:
    void        get_script_path(str_base& out, bool readable) const;
    desc        m_desc;
};
