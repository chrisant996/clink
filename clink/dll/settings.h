// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
enum setting_type
{
    SETTING_TYPE_ERROR = -1,

    // int types.
    SETTING_TYPE_BOOL,
    SETTING_TYPE_INT,
    SETTING_TYPE_ENUM,

    // cstr types.
    SETTING_TYPE_STR,
    SETTING_TYPE_PATH,

    SETTING_TYPE_COUNT,

    SETTING_TYPE_INTS       = SETTING_TYPE_BOOL,
    SETTING_TYPE_STRINGS    = SETTING_TYPE_STR,
};
typedef enum setting_type setting_type_e;

//------------------------------------------------------------------------------
struct setting_decl
{
    const char*     name;
    const char*     friendly_name;
    const char*     description;
    setting_type_e  type;
    const char*     type_param;
    const char*     default_value;
};
typedef struct setting_decl setting_decl_t;

typedef struct settings settings_t;

//------------------------------------------------------------------------------
settings_t*           settings_init(const setting_decl_t* decls, int decl_count);
void                  settings_shutdown(settings_t* s);
void                  settings_reset(settings_t* s);
int                   settings_load(settings_t* s, const char* file);
int                   settings_save(settings_t* s, const char* file);
void                  settings_gui(settings_t* s);
int                   settings_get_int(settings_t* s, const char* name);
const char*           settings_get_str(settings_t* s, const char* name);
void                  settings_set_int(settings_t* s, const char* name, int value);
void                  settings_set_str(settings_t* s, const char* name, const char* value);
void                  settings_set(settings_t* s, const char* name, const char* value);
const setting_decl_t* settings_get_decls(settings_t* s);
const setting_decl_t* settings_get_decl_by_name(settings_t* s, const char* name);
int                   settings_get_decl_count(settings_t* s);
