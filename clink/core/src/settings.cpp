// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "settings.h"
#include "str.h"
#include "str_tokeniser.h"
#include "path.h"

#include <assert.h>
#include <string>
#include <map>

//------------------------------------------------------------------------------
struct loaded_setting
{
                    loaded_setting() : saved(false) {}

    std::string     comment;
    std::string     value;
    bool            saved;
};

//------------------------------------------------------------------------------
static setting_map* g_setting_map = nullptr;
static std::map<std::string, loaded_setting> g_loaded_settings;

//------------------------------------------------------------------------------
static auto& get_map()
{
    if (!g_setting_map)
        g_setting_map = new setting_map;
    return *g_setting_map;
}



//------------------------------------------------------------------------------
setting_iter::setting_iter(setting_map& map)
: m_map(map)
, m_iter(map.begin())
{
}

//------------------------------------------------------------------------------
setting* setting_iter::next()
{
    if (m_iter == m_map.end())
        return nullptr;

    auto* i = m_iter->second;
    m_iter++;
    return i;
}



namespace settings
{

//------------------------------------------------------------------------------
setting_iter first()
{
    return setting_iter(get_map());
}

//------------------------------------------------------------------------------
setting* find(const char* name)
{
    auto i = get_map().find(name);
    if (i != get_map().end())
        return i->second;

    return nullptr;
}

//------------------------------------------------------------------------------
static bool set_setting(const char* name, const char* value, const char* comment=nullptr)
{
    // Find the setting.
    setting* s = settings::find(name);

    // Remember the original text from the file, so that saving won't lose
    // them in case the scripts that declared them aren't loaded.
    loaded_setting loaded;
    if (comment)
        loaded.comment = comment;
    loaded.value = value;
    g_loaded_settings.emplace(name, std::move(loaded));

    // Set its value.
    return s && s->set(value);
}

//------------------------------------------------------------------------------
static bool migrate_setting(const char* name, const char* value)
{
    // `esc_clears_line` is no longer a setting; bind `\e[27;27~` to whatever
    // command is desired in the inputrc file (defaults to `clink-reset-line`).
    //
    // `match_colour` is no longer a setting; use `colored-stats` in the inputrc
    // file and `set LS_COLORS` to set the colors.  Also certain `color.*` Clink
    // settings.
    //
    // `use_altgr_substitute` is no longer a setting.

    if (stricmp(name, "exec_match_style") == 0)
    {
        int x = atoi(value);
        set_setting("exec.path", (x>=0) ? "1" : "0");
        set_setting("exec.cwd",  (x>=1) ? "1" : "0");
        set_setting("exec.dirs", (x>=2) ? "1" : "0");
        return true;
    }
    else if (stricmp(name, "prompt_colour") == 0)
    {
        int attr = atoi(value);
        if (attr < 0)
            return false;
        static const char* const dos_color_names[] = { "bla", "blu", "red", "cya", "gre", "mag", "yel", "whi" };
        str<> tmp;
        if (attr & 0x08)
            tmp << "bri ";
        tmp << dos_color_names[attr & 0x07];
        return set_setting("color.prompt", tmp.c_str());
    }
    else if (stricmp(name, "strip_crlf_on_paste") == 0)
    {
        switch (atoi(value))
        {
        case 0: return false; // There is no equivalent at this time.
        case 1: value = "delete"; break;
        case 2: value = "space"; break;
        }
        name = "clink.paste_crlf";
    }
    else if (stricmp(name, "ansi_code_support") == 0)
    {
        name = "terminal.emulation";
        value = atoi(value) ? "auto" : "native";
    }
    else
    {
        static constexpr struct {
            const char* old_name;
            const char* new_name;
        } map_names[] =
        {   // OLD NAME                      NEW NAME
            { "ctrld_exits",                "cmd.ctrld_exits" },
            { "space_prefix_match_files",   "exec.space_prefix" },
            { "terminate_autoanswer",       "cmd.auto_answer" },
            { "history_file_lines",         "history.max_lines" },
            { "history_ignore_space",       "history.ignore_space" },
            { "history_dupe_mode",          "history.dupe_mode" },
            { "history_io",                 "history.shared" },
            { "history_expand_mode",        "history.expand_mode" },
        };

        const char* old_name = name;
        name = nullptr;

        for (auto map_name : map_names)
            if (stricmp(old_name, map_name.old_name) == 0)
            {
                name = map_name.new_name;
                break;
            }

        if (!name)
            return false;
    }

    return set_setting(name, value);
}

//------------------------------------------------------------------------------
static bool save_internal(const char* file, bool migrating);

//------------------------------------------------------------------------------
bool load(const char* file)
{
    g_loaded_settings.clear();

    // Maybe migrate settings.
    str<> old_file;
    bool migrating = false;

    // Open the file.
    FILE* in = fopen(file, "rb");
    if (in == nullptr)
    {
        // If there's no (new name) settings file, try to migrate from the old
        // name settings file.
        path::get_directory(file, old_file);
        path::append(old_file, "settings");
        in = fopen(old_file.c_str(), "rb");
        if (in == nullptr)
            return false;
        migrating = true;
    }

    // Buffer the file.
    fseek(in, 0, SEEK_END);
    int size = ftell(in);
    fseek(in, 0, SEEK_SET);

    if (size == 0)
    {
        fclose(in);
        return false;
    }

    str<4096> buffer;
    buffer.reserve(size);

    char* data = buffer.data();
    fread(data, size, 1, in);
    fclose(in);
    data[size] = '\0';

    // Reset settings to default.
    for (auto iter = settings::first(); auto* next = iter.next();)
        next->set();

    // Split at new lines.
    bool was_comment = false;
    str<> comment;
    str<256> line;
    str_tokeniser lines(buffer.c_str(), "\n\r");
    while (lines.next(line))
    {
        char* line_data = line.data();

        // Clear the comment accumulator after a non-comment line.
        if (!was_comment)
            comment.clear();

        // Skip line's leading whitespace.
        while (isspace(*line_data))
            ++line_data;

        // Comment?
        if (line_data[0] == '#')
        {
            was_comment = true;
            comment.concat(line_data);
            comment.concat("\n");
            continue;
        }

        // 'key = value'?
        was_comment = false;
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

        // Migrate old setting.
        if (migrating)
        {
            migrate_setting(line_data, value);
            continue;
        }

        // Find the setting and set its value.
        set_setting(line_data, value, comment.c_str());
    }

    // When migrating, ensure the new settings file is created so that the old
    // settings file can be deleted.  Some users or distributions may naturally
    // clean up the old settings file, so don't rely on it staying around.
    if (migrating)
        save_internal(file, migrating);

    return true;
}

//------------------------------------------------------------------------------
static bool save_internal(const char* file, bool migrating)
{
    // Open settings file.  When migrating, fail if the file already exists, so
    // that if multiple concurrent migrations occur only one of them writes the
    // new settings file.
    FILE* out = fopen(file, migrating ? "wtx" : "wt");
    if (out == nullptr)
        return false;

    // Clear the saved flag so we can track which ones have been saved so far.
    for (auto iter : g_loaded_settings)
        iter.second.saved = false;

    // Iterate over each setting and write it out to the file.
    for (auto i : get_map())
    {
        setting* iter = i.second;
        auto loaded = g_loaded_settings.find(iter->get_name());
        if (loaded != g_loaded_settings.end())
            loaded->second.saved = true;

        // Don't write out settings that aren't modified from their defaults.
        if (iter->is_default())
            continue;

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
        case setting::type_color:  type_name = "color";   break;
        }

        if (type_name != nullptr)
            fprintf(out, "# type: %s\n", type_name);

        // Output an enum-type setting's options.
        if (type == setting::type_enum)
        {
            const setting_enum* as_enum = (setting_enum*)iter;
            fprintf(out, "# options: %s\n", as_enum->get_options());
        }

        str<> value;
        iter->get_descriptive(value);
        fprintf(out, "%s = %s\n\n", iter->get_name(), value.c_str());
    }

    // Iterate over loaded settings and write out any that weren't saved yet.
    // This prevents losing user settings when some scripts aren't loaded, e.g.
    // by changing the script path.
    bool first_extra = true;
    for (const auto iter : g_loaded_settings)
        if (!iter.second.saved)
        {
            if (first_extra)
            {
                first_extra = false;
                fputs("\n\n", out);
            }
            fprintf(out, "%s%s = %s\n\n", iter.second.comment.c_str(), iter.first.c_str(), iter.second.value.c_str());
        }

    fclose(out);
    return true;
}

//------------------------------------------------------------------------------
bool save(const char* file)
{
    return save_internal(file, false/*migrating*/);
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
, m_type(type)
{
    assert(strlen(short_desc) == m_short_desc.length());
    assert(!settings::find(m_name.c_str()));

    get_map()[m_name.c_str()] = this;
}

//------------------------------------------------------------------------------
setting::~setting()
{
    auto i = settings::find(m_name.c_str());

    if (i && i == this)
        get_map().erase(m_name.c_str());
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
const char* setting::get_loaded_value(const char* name)
{
    auto const loaded = g_loaded_settings.find(name);
    if (loaded == g_loaded_settings.end())
        return nullptr;
    return loaded->second.value.c_str();
}



//------------------------------------------------------------------------------
template <> bool setting_impl<bool>::set(const char* value)
{
    if (stricmp(value, "true") == 0)  { m_store.value = 1; return true; }
    if (stricmp(value, "false") == 0) { m_store.value = 0; return true; }

    if (stricmp(value, "on") == 0)    { m_store.value = 1; return true; }
    if (stricmp(value, "off") == 0)   { m_store.value = 0; return true; }

    if (stricmp(value, "yes") == 0)   { m_store.value = 1; return true; }
    if (stricmp(value, "no") == 0)    { m_store.value = 0; return true; }

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
    if ((*value < '0' || *value > '9') && *value != '-')
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
    int by_int = -1;
    if (*value >= '0' && *value <= '9')
    {
        by_int = atoi(value);
        for (const char* walk = value; *walk; walk++)
            if (*walk < '0' || *walk > '9')
            {
                by_int = -1;
                break;
            }
    }

    int i = 0;
    for (const char* option = m_options.c_str(); *option; ++i)
    {
        const char* next = next_option(option);

        int option_len = int(next - option);
        if (*next)
            --option_len;

        if ((by_int == 0) ||
            (by_int < 0 && _strnicmp(option, value, option_len) == 0))
        {
            m_store.value = i;
            return true;
        }

        by_int--;

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

//------------------------------------------------------------------------------
static bool imatch3(const str<16>& a, const char* b)
{
    return _strnicmp(a.c_str(), b, 3) == 0;
}

//------------------------------------------------------------------------------
setting_color::setting_color(const char* name, const char* short_desc, const char* default_value)
: setting_str(name, short_desc, default_value)
{
    m_type = type_color;
    set();
}

//------------------------------------------------------------------------------
setting_color::setting_color(const char* name, const char* short_desc, const char* long_desc, const char* default_value)
: setting_str(name, short_desc, long_desc, default_value)
{
    m_type = type_color;
    set();
}

//------------------------------------------------------------------------------
bool setting_color::is_default() const
{
    str<> value;
    get_descriptive(value);
    return value.equals(static_cast<const char*>(m_default));
}

//------------------------------------------------------------------------------
void setting_color::set()
{
    set(static_cast<const char*>(m_default));
}

//------------------------------------------------------------------------------
static const char* const color_names[] = { "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white" };

//------------------------------------------------------------------------------
bool setting_color::set(const char* value)
{
    if (!value || !*value)
        return setting_str::set("");

    str<> code;
    str<16> token;
    int fg = -1;
    int bg = -1;
    int bold = -1;
    int bright = -1;
    int underline = -1;
    int* pcolor = &fg;
    bool saw_default = false;
    bool first_part = true;

    str_iter part;
    str_tokeniser parts(value, " ");
    while (parts.next(part))
    {
        token.clear();
        token.concat(part.get_pointer(), part.length());

        if (first_part && (strcmpi(token.c_str(), "ansi") == 0 ||
                           strcmpi(token.c_str(), "sgr") == 0))
        {
            if (parts.next(part))
                code.concat(part.get_pointer(), part.length());
            if (parts.next(part)) // too many tokens
                return false;
            return setting_str::set(code.c_str());
        }

        first_part = false;

        if (strcmpi(token.c_str(), "on") == 0)
        {
            if (pcolor == &bg) return false; // can't use "on" more than once
            if (bright > 0)
                fg += 8;
            pcolor = &bg;
            bright = -1;
            saw_default = false;
            continue;
        }

        if (imatch3(token, "normal") || imatch3(token, "default"))
        {
            if (*pcolor >= 0) return false; // disallow combinations
            if (saw_default) return false;
            *pcolor = -1;
            saw_default = true;
            continue;
        }

        if (imatch3(token, "bold") || imatch3(token, "nobold") || imatch3(token, "dim"))
        {
            if (pcolor == &bg) return false; // bold only applies to fg
            if (bold >= 0) return false; // disallow combinations
            bold = imatch3(token, "bold");
            continue;
        }

        if (imatch3(token, "bright"))
        {
            if (bright >= 0) return false; // disallow combinations
            bright = imatch3(token, "bright");
            continue;
        }

        if (imatch3(token, "underline") || imatch3(token, "nounderline"))
        {
            if (pcolor == &bg) return false; // only applies to fg
            if (underline >= 0) return false; // disallow combinations
            underline = imatch3(token, "underline");
            continue;
        }

        int i;
        for (i = 0; i < sizeof_array(color_names); i++)
            if (_strnicmp(token.c_str(), color_names[i], 3) == 0)
            {
                if (*pcolor >= 0) return false; // disallow combinations
                *pcolor = i;
                break;
            }

        if (i >= sizeof_array(color_names)) // unrecognized keyword
            return false;
    }

    if (*pcolor >= 0 && bright > 0)
        (*pcolor) += 8;

    code = "0";

    if (bold > 0)
        code << ";1";
    else if (bold == 0)
        code << ";22";

    if (underline > 0)
        code << ";4";
    else if (underline == 0)
        code << ";24";

    if (fg >= 0)
    {
        if (fg >= 8)
            fg += 60 - 8;
        fg += 30;
        char buf[10];
        itoa(fg, buf, 10);
        code << ";" << buf;
    }
    else
    {
        code << ";39";
    }

    if (bg >= 0)
    {
        if (bg >= 8)
            bg += 60 - 8;
        bg += 40;
        char buf[10];
        itoa(bg, buf, 10);
        code << ";" << buf;
    }
    else
    {
        code << ";49";
    }

    return setting_str::set(code.c_str());
}

//------------------------------------------------------------------------------
static int int_from_str_iter(const str_iter& iter)
{
    int x = 0;
    int c = iter.length();
    for (const char* p = iter.get_pointer(); c--; p++)
    {
        if (*p < '0' || *p > '9')
            return -1;
        x *= 10;
        x += *p - '0';
    }
    return x;
}

//------------------------------------------------------------------------------
static bool strip_if_ends_with(str_base& s, const char* suffix, unsigned int len)
{
    if (s.length() > len && strcmp(s.c_str() + s.length() - len, suffix) == 0)
    {
        s.truncate(s.length() - len);
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
void setting_color::get_descriptive(str_base& out) const
{
    str<> tmp;
    get(tmp);

    out.clear();
    if (tmp.empty())
        return;

    enum { reset_token, bold_token, underline_token, fg_token, bg_token, nomore_tokens };
    int expected = reset_token;
    str_iter part;
    str_tokeniser parts(tmp.c_str(), ";");

    while (parts.next(part))
    {
        int x = int_from_str_iter(part);
        if (x < 0)
        {
nope:
            out.clear();
            out << "sgr " << tmp.c_str();
            return;
        }

        if (expected >= nomore_tokens)
            goto nope;
        if (expected == reset_token && x != 0)
            goto nope;

        if (x == 0)
        {
            if (expected > reset_token) goto nope;
            expected = reset_token + 1;
        }
        else if (x == 1 || x == 22)
        {
            if (expected > bold_token) goto nope;
            expected = bold_token + 1;
            out << ((x == 1) ? "bold " : "nobold ");
        }
        else if (x == 4 || x == 24)
        {
            if (expected > underline_token) goto nope;
            expected = underline_token + 1;
            out << ((x == 4) ? "underline " : "nounderline ");
        }
        else if ((x >= 30 && x < 38) || (x >= 90 && x < 98))
        {
            if (expected > fg_token) goto nope;
            expected = fg_token + 1;
            if (x >= 90)
            {
                out << "bright ";
                x -= 60;
            }
            out << color_names[x - 30] << " ";
        }
        else if (x == 39)
        {
            if (expected > fg_token) goto nope;
            expected = fg_token + 1;
            out << "default ";
        }
        else if ((x >= 40 && x < 48) || (x >= 100 && x < 108))
        {
            if (expected > bg_token) goto nope;
            expected = bg_token + 1;
            out << "on ";
            x -= 10;
            if (x >= 90)
            {
                out << "bright ";
                x -= 60;
            }
            out << color_names[x - 30] << " ";
        }
        else if (x == 49)
        {
            if (expected > bg_token) goto nope;
            expected = bg_token + 1;
            out << "on default ";
        }
        else
            goto nope;
    }

    if (out.empty() || out.equals("default on default "))
        out = "default";
    else if (!strip_if_ends_with(out, "default on default ", 19))
        strip_if_ends_with(out, "on default ", 11);

    while (out.length() && out.c_str()[out.length() - 1] == ' ')
        out.truncate(out.length() - 1);
}
