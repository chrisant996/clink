// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "paths.h"

#include <core/settings.h>
#include <core/str.h>

//------------------------------------------------------------------------------
static void get_settings_file(str_base& buffer)
{
    get_config_dir(buffer);
    buffer << "/settings";
}

//------------------------------------------------------------------------------
void load_clink_settings()
{
    str<MAX_PATH> settings_file;
    get_settings_file(settings_file);

    if (!settings::load(settings_file.c_str()))
        settings::save(settings_file.c_str());
}

//------------------------------------------------------------------------------
void save_clink_settings()
{
    str<MAX_PATH> settings_file;
    get_settings_file(settings_file);
    settings::save(settings_file.c_str());
}
