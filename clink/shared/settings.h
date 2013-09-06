/* Copyright (c) 2012 Martin Ridgers
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

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

#endif // SETTINGS_H

// vim: expandtab
