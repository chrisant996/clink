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

#include "pch.h"
#include "settings.h"
#include "util.h"

//------------------------------------------------------------------------------
void wrapped_write(FILE*, const char*, const char*, int);

//------------------------------------------------------------------------------
static const char* g_type_names[SETTING_TYPE_COUNT] = {
    "bool",
    "int",
    "enum",
    "string",
    "path",
};

//------------------------------------------------------------------------------
struct settings
{
    int                     count;
    const setting_decl_t*   decls;
    char**                  values;
};
typedef struct settings settings_t;

//------------------------------------------------------------------------------
static int decl_is_string_type(const setting_decl_t* decl)
{
    int i = decl->type;
    return (i >= SETTING_TYPE_STRINGS) && (i < SETTING_TYPE_COUNT);
}

//------------------------------------------------------------------------------
static void set_value(
    settings_t* s,
    const setting_decl_t* decl,
    const char* value
)
{
    char* new_str;
    const char* str;
    int len;

    int i = (int)(decl - s->decls);
    int type = decl->type;

    // Oob check.
    if (type < SETTING_TYPE_INTS || type >= SETTING_TYPE_COUNT)
    {
        return;
    }

    // C-string type value.
    str = value;
    len = (int)strlen(str) + 1;

    new_str = malloc(len);
    str_cpy(new_str, str, len);

    free(s->values[i]);
    s->values[i] = new_str;
}

//------------------------------------------------------------------------------
const setting_decl_t* settings_get_decl_by_name(settings_t* s, const char* name)
{
    int i;
    for (i = 0; i < s->count; ++i)
    {
        const setting_decl_t* decl = s->decls + i;
        if (stricmp(decl->name, name) == 0)
        {
            return decl;
        }
    }

    return NULL;
}

//------------------------------------------------------------------------------
static int get_decl_index(settings_t* s, const char* name)
{
    const setting_decl_t* decl = settings_get_decl_by_name(s, name);
    if (decl != NULL)
    {
        return (int)(decl - s->decls);
    }

    return -1;
}

//------------------------------------------------------------------------------
static const char* get_decl_default(settings_t* s, const char* name)
{
    const setting_decl_t* decl = settings_get_decl_by_name(s, name);
    if (decl != NULL)
    {
        return decl->default_value;
    }

    return "";
}

//------------------------------------------------------------------------------
settings_t* settings_init(const setting_decl_t* decls, int decl_count)
{
    settings_t* s;

    s = malloc(sizeof(settings_t));
    s->count = decl_count;
    s->decls = decls;

    s->values = calloc(sizeof(char*), decl_count);
    settings_reset(s);

    return s;
}

//------------------------------------------------------------------------------
void settings_shutdown(settings_t* s)
{
    int i;

    for (i = 0; i < s->count; ++i)
    {
        if (!decl_is_string_type(s->decls + i))
        {
            continue;
        }

        free((void*)s->values[i]);
    }

    free(s->values);
    free(s);
}

//------------------------------------------------------------------------------
void settings_reset(settings_t* s)
{
    int i;
    const setting_decl_t* decl;
    
    decl = s->decls;
    for (i = 0; i < s->count; ++i)
    {
        set_value(s, decl, decl->default_value);
        ++decl;
    }
}

//------------------------------------------------------------------------------
int settings_load(settings_t* s, const char* file)
{
    int i;
    FILE* in;
    char* data;
    char* line;

    // Open the file.
    in = fopen(file, "rb");
    if (in == NULL)
    {
        return 0;
    }

    // Buffer the file.
    fseek(in, 0, SEEK_END);
    i = ftell(in);
    fseek(in, 0, SEEK_SET);

    data = malloc(i + 1);
    fread(data, i, 1, in);
    fclose(in);
    data[i] = '\0';

    // Split at new lines.
    line = strtok(data, "\n\r");
    while (line != NULL && *line)
    {
        char* c;

        // Skip line's leading whitespace.
        while (isspace(*line))
        {
            ++line;
        }
            
        c = strchr(line, '=');
        if (c != NULL && *line != '#')
        {
            char* d;
            const setting_decl_t* decl;

            *c++ = '\0';

            // Trim whitespace.
            d = c - 2;
            while (d >= line && isspace(*d))
            {
                --d;
            }
            *(d + 1) = '\0';

            while (*c && isspace(*c))
            {
                ++c;
            }            

            decl = settings_get_decl_by_name(s, line);
            if (decl != NULL)
            {
                set_value(s, decl, c);
            }
        }

        line = strtok(NULL, "\n\r");
    }

    free(data);
    return 1;
}

//------------------------------------------------------------------------------
int settings_save(settings_t* s, const char* file)
{
    int i;
    FILE* out;

    // Open settings file.
    out = fopen(file, "wt");
    if (out == NULL)
    {
        return 0;
    }

    // Iterate over each setting and write it out to the file.
    for (i = 0; i < s->count; ++i)
    {
        const setting_decl_t* decl;

        decl = s->decls + i;

        fprintf(out, "# name: %s\n", decl->friendly_name);
        fprintf(out, "# type: %s\n", g_type_names[decl->type]);

        if (decl->type == SETTING_TYPE_ENUM)
        {
            int j = 0;
            const char* param = decl->type_param;
            while (*param)
            {
                fprintf(out, "# %2d = %s\n", j, param);
                param += strlen(param) + 1;
                ++j;
            }
        }

        wrapped_write(out, "# ", decl->description, 78);
        fprintf(out, "%s = %s\n\n", decl->name, s->values[i]);
    }

    fclose(out);
    return 1;
}

//------------------------------------------------------------------------------
const char* settings_get_str(settings_t* s, const char* name)
{
    int i;

    // Check for an environment variable override.
    {
        static char buffer[256];

        strcpy(buffer, "clink.");
        str_cat(buffer, name, sizeof_array(buffer));

        if (GetEnvironmentVariableA(buffer, buffer, sizeof_array(buffer)))
        {
            return buffer;
        }
    }

    i = get_decl_index(s, name);
    if (i != -1)
    {
        return s->values[i];
    }

    return "";
}

//------------------------------------------------------------------------------
int settings_get_int(settings_t* s, const char* name)
{
    return atoi(settings_get_str(s, name));
}

//------------------------------------------------------------------------------
void settings_set_int(settings_t* s, const char* name, int value)
{
    const setting_decl_t* decl = settings_get_decl_by_name(s, name);
    if (decl != NULL)
    {
        char buffer[32];
        itoa(value, buffer, 10);
        set_value(s, decl, buffer);
    }
}

//------------------------------------------------------------------------------
void settings_set_str(settings_t* s, const char* name, const char* value)
{
    const setting_decl_t* decl = settings_get_decl_by_name(s, name);
    if (decl != NULL)
    {
        set_value(s, decl, value);
    }
}

//------------------------------------------------------------------------------
void settings_set(settings_t* s, const char* name, const char* value)
{
    const setting_decl_t* decl = settings_get_decl_by_name(s, name);
    if (decl != NULL)
    {
        if (decl_is_string_type(decl))
        {
            set_value(s, decl, value);
        }
        else
        {
            int i;
            i = atoi(value);
            settings_set_int(s, name, i);
        }
    }
}

//------------------------------------------------------------------------------
const setting_decl_t* settings_get_decls(settings_t* s)
{
    return s->decls;
}

//------------------------------------------------------------------------------
int settings_get_decl_count(settings_t* s)
{
    return s->count;
}

// vim: expandtab
