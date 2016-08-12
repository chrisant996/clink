// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "settings.h"
#include "str.h"
#include "str_tokeniser.h"

//------------------------------------------------------------------------------
static setting* g_setting_list = nullptr;



namespace settings
{

//------------------------------------------------------------------------------
setting* first()
{
    return g_setting_list;
}

//------------------------------------------------------------------------------
setting* find(const char* name)
{
    setting* next = first();
    do
    {
        if (stricmp(name, next->get_name()) == 0)
            return next;
    }
    while (next = next->next());

    return nullptr;
}

//------------------------------------------------------------------------------
bool load(const char* file)
{
    // Open the file.
    FILE* in = fopen(file, "rb");
    if (in == nullptr)
        return false;

    // Buffer the file.
    fseek(in, 0, SEEK_END);
    int size = ftell(in);
    fseek(in, 0, SEEK_SET);

    if (size == 0)
        return false;

    str<4096> buffer;
    buffer.reserve(size);

    char* data = buffer.data();
    fread(data, size, 1, in);
    fclose(in);
    data[size] = '\0';

    // Split at new lines.
    str<256> line;
    str_tokeniser lines(buffer.c_str(), "\n\r");
    while (lines.next(line))
    {
        char* line_data = line.data();

        // Skip line's leading whitespace.
        while (isspace(*line_data))
            ++line_data;

        // Comment?
        if (line_data[0] == '#')
            continue;

        // 'key = value'?
        char* value = strchr(line_data, '=');
        if (value == nullptr)
            continue;

        *value++ = '\0';

        // Trim whitespace.
        char* key_end = value - 2;
        while (key_end >= line_data && isspace(*key_end))
            --key_end;
        key_end[1] = '\0';

        while (*value && isspace(*value))
            ++value;

        // Find the setting and set its value.
        if (setting* s = settings::find(line_data))
            s->set(value);
    }

    return true;
}

//------------------------------------------------------------------------------
bool save(const char* file)
{
    // Open settings file.
    FILE* out = fopen(file, "wt");
    if (out == nullptr)
        return false;

    // Iterate over each setting and write it out to the file.
    for (const setting* iter = settings::first(); iter != nullptr; iter = iter->next())
    {
        fprintf(out, "# name: %s\n", iter->get_short_desc());

        // Write out the setting's type.
        int type = iter->get_type();
        const char* type_name = nullptr;
        switch (type)
        {
        case setting::type_bool:   type_name = "boolean"; break;
        case setting::type_int:    type_name = "integer"; break;
        case setting::type_string: type_name = "string";  break;
        case setting::type_enum:   type_name = "enum";    break;
        }

        if (type_name != nullptr)
            fprintf(out, "# type: %s\n", type_name);

        // Output an enum-type setting's options.
        if (type == setting::type_enum)
        {
            fprintf(out, "# options:");

            const setting_enum* as_enum = (setting_enum*)iter;
            for (const char* option = as_enum->get_options(); *option; )
            {
                fprintf(out, " %s", option);
                while (*option++);
            }

            fprintf(out, "\n");
        }

        str<> value;
        iter->get(value);
        fprintf(out, "%s = %s\n\n", iter->get_name(), value.c_str());
    }

    fclose(out);
    return true;
}

} // namespace settings



//------------------------------------------------------------------------------
setting::setting(
    const char* name,
    const char* short_desc,
    const char* long_desc,
    type_e type)
: m_name(name)
, m_short_desc(short_desc)
, m_long_desc(long_desc ? long_desc : "")
, m_next(g_setting_list)
, m_prev(nullptr)
, m_type(type)
{
    g_setting_list = this;
    if (m_next != nullptr)
        m_next->m_prev = this;
}

//------------------------------------------------------------------------------
setting::~setting()
{
    if (m_prev != nullptr)
        m_prev->m_next = m_next;
    else
        g_setting_list = m_next;

    if (m_next != nullptr)
        m_next->m_prev = m_prev;
}

//------------------------------------------------------------------------------
setting* setting::next() const
{
    return m_next;
}

//------------------------------------------------------------------------------
setting::type_e setting::get_type() const
{
    return m_type;
}

//------------------------------------------------------------------------------
const char* setting::get_name() const
{
    return m_name.c_str();
}

//------------------------------------------------------------------------------
const char* setting::get_short_desc() const
{
    return m_short_desc.c_str();
}

//------------------------------------------------------------------------------
const char* setting::get_long_desc() const
{
    return m_long_desc.c_str();
}



//------------------------------------------------------------------------------
template <> bool setting_impl<bool>::set(const char* value)
{
    if (stricmp(value, "true") == 0)  { m_store.value = 1; return true; }
    if (stricmp(value, "false") == 0) { m_store.value = 0; return true; }

    if (*value >= '0' && *value <= '9')
    {
        m_store.value = !!atoi(value);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
template <> bool setting_impl<int>::set(const char* value)
{
    if (*value < '0' || *value > '9')
        return false;

    m_store.value = atoi(value);
    return true;
}

//------------------------------------------------------------------------------
template <> bool setting_impl<const char*>::set(const char* value)
{
    m_store.value = value;
    return true;
}



//------------------------------------------------------------------------------
template <> void setting_impl<bool>::get(str_base& out) const
{
    out = m_store.value ? "True" : "False";
}

//------------------------------------------------------------------------------
template <> void setting_impl<int>::get(str_base& out) const
{
    out.format("%d", m_store.value);
}

//------------------------------------------------------------------------------
template <> void setting_impl<const char*>::get(str_base& out) const
{
    out = m_store.value.c_str();
}



//------------------------------------------------------------------------------
setting_enum::setting_enum(
    const char* name,
    const char* short_desc,
    const char* options,
    int default_value)
: setting_enum(name, short_desc, nullptr, options, default_value)
{
}

//------------------------------------------------------------------------------
setting_enum::setting_enum(
    const char* name,
    const char* short_desc,
    const char* long_desc,
    const char* options,
    int default_value)
: setting_impl<int>(name, short_desc, long_desc, default_value)
, m_options(options)
{
    m_type = type_enum;
}

//------------------------------------------------------------------------------
bool setting_enum::set(const char* value)
{
    int i = 0;
    for (const char* option = m_options.c_str(); *option; ++i)
    {
        const char* next = next_option(option);

        int option_len = int(next - option);
        if (*next)
            --option_len;

        if (_strnicmp(option, value, option_len) == 0)
        {
            m_store.value = i;
            return true;
        }

        option = next;
    }

    return false;
}

//------------------------------------------------------------------------------
void setting_enum::get(str_base& out) const
{
    int index = m_store.value;
    if (index < 0)
        return;

    const char* option = m_options.c_str();
    for (int i = 0; i < index && *option; ++i)
        option = next_option(option);

    if (*option)
    {
        const char* next = next_option(option);
        if (*next)
            --next;

        out.clear();
        out.concat(option, int(next - option));
    }
}

//------------------------------------------------------------------------------
const char* setting_enum::get_options() const
{
    return m_options.c_str();
}

//------------------------------------------------------------------------------
const char* setting_enum::next_option(const char* option)
{
    while (*option)
        if (*option++ == ',')
            break;

    return option;
}
