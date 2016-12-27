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
        bool    quiet = false;
        bool    log = true;
        char    state_dir[512]; // = {}; (this crashes cl.exe v18.00.21005.1)
    };

                app_context(const desc& desc);
    int         get_id() const;
    bool        is_logging_enabled() const;
    bool        is_quiet() const;
    void        get_binaries_dir(str_base& out) const;
    void        get_state_dir(str_base& out) const;
    void        get_log_path(str_base& out) const;
    void        get_settings_path(str_base& out) const;
    void        get_history_path(str_base& out) const;

private:
    bool        load_from_env();
    void        store_to_env();
    desc        m_desc;
    int         m_id = 0;
};
