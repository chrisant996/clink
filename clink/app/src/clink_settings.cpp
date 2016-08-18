// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "utils/app_context.h"

#include <core/settings.h>
#include <core/str.h>

//------------------------------------------------------------------------------
void load_clink_settings()
{
    str<MAX_PATH> settings_file;
    app_context::get()->get_settings_path(settings_file);

    if (!settings::load(settings_file.c_str()))
        settings::save(settings_file.c_str());
}

//------------------------------------------------------------------------------
void save_clink_settings()
{
    str<MAX_PATH> settings_file;
    app_context::get()->get_settings_path(settings_file);
    settings::save(settings_file.c_str());
}
