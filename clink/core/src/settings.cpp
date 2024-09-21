// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "settings.h"
#include "str.h"
#include "str_tokeniser.h"
#include "str_compare.h"
#include "path.h"
#include "os.h"

#include <assert.h>
#include <string>
#include <map>
#include <functional>

#include "debugheap.h"

//------------------------------------------------------------------------------
struct loaded_setting
{
                    loaded_setting() : saved(false) {}

    std::string     comment;
    std::string     value;
    bool            saved;
};

typedef std::map<std::string, loaded_setting> loaded_settings_map;

//------------------------------------------------------------------------------
static setting_map* g_setting_map = nullptr;
static loaded_settings_map* g_loaded_settings = nullptr;
static loaded_settings_map* g_custom_defaults = nullptr;
static str_moveable* g_last_file = nullptr;

bool g_force_break_on_error = false;

#ifdef DEBUG
static bool s_ever_loaded = false;
#endif

//------------------------------------------------------------------------------
static auto& get_map()
{
    if (!g_setting_map)
        g_setting_map = new setting_map;
    return *g_setting_map;
}

static auto& get_loaded_map()
{
    if (!g_loaded_settings)
        g_loaded_settings = new loaded_settings_map;
    return *g_loaded_settings;
}

static auto& get_custom_default_map()
{
    if (!g_custom_defaults)
        g_custom_defaults = new loaded_settings_map;
    return *g_custom_defaults;
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



//------------------------------------------------------------------------------
static bool load_internal(FILE* in, std::function<void(const char* name, const char* value, const char* comment)> load_setting)
{
    dbg_ignore_scope(snapshot, "Settings");

    // Buffer the file.
    fseek(in, 0, SEEK_END);
    int32 size = ftell(in);
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

    // Check for `clink set` output format:
    // No blank lines, no # lines, no = signs.
    str<256> line;
    bool maybe_clink_set = true;
    {
        str_tokeniser lines(buffer.c_str(), "\n\r");
        while (lines.next(line))
        {
            const char* p = line.c_str();
            if (!*p || *p == '#' || strchr(p, '='))
            {
                maybe_clink_set = false;
                break;
            }
        }
    }

    // Split at new lines.
    bool was_comment = false;
    str<> comment;
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
        char* key_end = strchr(line_data, '=');
        if (key_end == nullptr)
        {
            if (!maybe_clink_set)
                continue;
            // If there's no = then delimit by whitespace.  This enables
            // saving the output from `clink set` to "clink_settings" for
            // troubleshooting purposes.
            //
            // WARNING:  This should not be used for anything other than
            // troubleshooting purposes; it isn't the real file format, and it
            // may not work properly if a setting name contains a space, etc.
            //
            key_end = strchr(line_data, ' ');
            if (key_end == nullptr)
                continue;
        }

        *key_end = '\0';
        const char* value = key_end + 1;

        // Trim whitespace.
        while (--key_end >= line_data && isspace(*key_end))
        {}
        key_end[1] = '\0';

        while (*value && isspace(*value))
            ++value;

        // Ugh, migrate clink.autoupdate setting from bool to enum.
        if (_strcmpi(line_data, "clink.autoupdate") == 0)
        {
            str<16> tmp(value);
            tmp.trim();
            if (tmp.iequals("false") || tmp.iequals("off") || tmp.iequals("no"))
                value = "0";
            else if (tmp.iequals("true") || tmp.iequals("on") || tmp.iequals("yes"))
                value = "1";
        }

        load_setting(line_data, value, comment.c_str());
    }

    return true;
}

//------------------------------------------------------------------------------
static bool parse_ini(FILE* in, std::vector<settings::setting_name_value>& out)
{
    dbg_ignore_scope(snapshot, "Settings ini file");

    // Buffer the file.
    fseek(in, 0, SEEK_END);
    int32 size = ftell(in);
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

    enum section { ignore, set, clear };
    section section = ignore;
    bool valid = false;

    // Split at new lines.
    str<256> line;
    str<32> key;
    str_tokeniser lines(buffer.c_str(), "\n\r");
    while (lines.next(line))
    {
        char* line_data = line.data();

        // Skip line's leading whitespace.
        while (isspace(*line_data))
            ++line_data;

        // Comment?
        if (line_data[0] == '#' || line_data[0] == ';')
            continue;

        // Section?
        if (line_data[0] == '[')
        {
            if (_strnicmp(line_data, "[set]", 5) == 0)
            {
                section = set;
                valid = true;
            }
            else if (_strnicmp(line_data, "[clear]", 7) == 0)
            {
                section = clear;
                valid = true;
            }
            else
                section = ignore;
            continue;
        }

        if (section == ignore)
            continue;

        // 'key = value'?
        char* value = nullptr;
        if (section == set)
        {
            value = strchr(line_data, '=');
            if (value == nullptr)
                return false;

            key.clear();
            key.concat(line_data, int32(value - line_data));
            key.trim();

            ++value;
            while (*value && isspace(*value))
                ++value;

            out.emplace_back(key.c_str(), value);
        }
        else
        {
            key = line_data;
            key.trim();

            out.emplace_back(key.c_str(), nullptr, true/*clear*/);
        }
    }

    return valid;
}

//------------------------------------------------------------------------------
// Mingw can't handle 'static' here, due to 'friend'.
/*static*/ void load_custom_defaults(const char* file)
{
    auto& map = get_custom_default_map();
    map.clear();

    if (!file || !*file)
        return;

    FILE* in = fopen(file, "rb");
    if (in == nullptr)
        return;

    load_internal(in, [&map](const char* name, const char* value, const char* comment)
    {
        loaded_setting custom_default;
        custom_default.value = value;
        map.emplace(name, std::move(custom_default));
    });

    fclose(in);
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

    size_t len = strlen(name);
    if (len > c_max_len_name)
    {
        str<> trunc_name(name);
        trunc_name.truncate(c_max_len_name);
        return find(trunc_name.c_str());
    }

    return nullptr;
}

//------------------------------------------------------------------------------
static bool set_setting(const char* name, const char* value, const char* comment=nullptr)
{
    // Find the setting.
    setting* s = settings::find(name);

    // Remember the original text from the file, so that saving won't lose
    // them in case the scripts that declared them aren't loaded.
    if (!s)
    {
        loaded_setting loaded;
        if (comment)
        {
            loaded.comment = comment;
        }
        else
        {
            const auto& l = get_loaded_map().find(name);
            if (l != get_loaded_map().end())
                loaded.comment = l->second.comment.c_str();
        }
        loaded.value = value;
        get_loaded_map().emplace(name, std::move(loaded));
        return true;
    }

    // Set its value.
    return s->set(value);
}

//------------------------------------------------------------------------------
static void clear_setting(const char* name)
{
    setting* s = settings::find(name);

    if (!s)
    {
        auto& map = get_loaded_map();
        const auto& i = map.find(name);
        if (i != map.end())
            map.erase(i);
        return;
    }

    // Clear its value.
    s->set();
}

//------------------------------------------------------------------------------
bool migrate_setting(const char* name, const char* value, std::vector<setting_name_value>& out)
{
    // `match_colour` is no longer a setting; use `colored-stats` in the inputrc
    // file and `set LS_COLORS` to set the colors.  Also certain `color.*` Clink
    // settings.

    out.clear();

    if (stricmp(name, "exec_match_style") == 0)
    {
        int32 x = value ? atoi(value) : 2;
        out.emplace_back("exec.enable", (x>=0) ? "1" : "0");
        if (x >= 0)
        {
            out.emplace_back("exec.path", (x>=0) ? "1" : "0");
            out.emplace_back("exec.cwd",  (x>=1) ? "1" : "0");
            out.emplace_back("exec.dirs", (x>=2) ? "1" : "0");
        }
        return true;
    }
    else if (stricmp(name, "prompt_colour") == 0)
    {
        int32 attr = value ? atoi(value) : -1;
        if (attr < 0)
        {
            if (!value)
            {
                out.emplace_back("color.prompt", "");
                return true;
            }
            return false;
        }
        static const char* const dos_color_names[] = { "bla", "blu", "red", "cya", "gre", "mag", "yel", "whi" };
        str<> tmp;
        if (attr & 0x08)
            tmp << "bri ";
        tmp << dos_color_names[attr & 0x07];
        out.emplace_back("color.prompt", tmp.c_str());
        return true;
    }
    else if (stricmp(name, "strip_crlf_on_paste") == 0)
    {
        switch (value ? atoi(value) : 2)
        {
        case 0: value = "crlf"; break;
        case 1: value = "delete"; break;
        case 2: value = "space"; break;
        }
        name = "clink.paste_crlf";
    }
    else if (stricmp(name, "ansi_code_support") == 0)
    {
        name = "terminal.emulation";
        value = (!value || atoi(value)) ? "auto" : "native";
    }
    else if (stricmp(name, "esc_clears_line") == 0)
    {
        name = "terminal.raw_esc";
        value = (!value || !atoi(value)) ? "1" : "0";
    }
    else if (stricmp(name, "history_file_lines") == 0)
    {
        int32 x = value ? atoi(value) : 2500;
        bool disable = x < 0;
        out.emplace_back("history.save", disable ? "0" : "1");
        if (!disable)
        {
            if (x > 0)
                out.emplace_back("history.max_lines", value);
            else if (x == 0)
                out.emplace_back("history.max_lines", "0"); // Unlimited.
            else
                out.emplace_back("history.max_lines", "10000");
        }
        return true;
    }
    else
    {
        static constexpr struct {
            const char* old_name;
            const char* new_name;
            const char* default_value;
        } map_names[] =
        {   // OLD NAME                      NEW NAME
            { "ctrld_exits",                "cmd.ctrld_exits",                  "1" },
            { "space_prefix_match_files",   "exec.space_prefix",                "1" },
            { "terminate_autoanswer",       "cmd.auto_answer",                  "0" },
            { "history_ignore_space",       "history.ignore_space",             "0" },
            { "history_dupe_mode",          "history.dupe_mode",                "2" },
            { "history_io",                 "history.shared",                   "0" },
            { "history_expand_mode",        "history.expand_mode",              "4" },
            { "use_altgr_substitute",       "terminal.use_altgr_substitute",    "1" },
        };

        const char* old_name = name;
        name = nullptr;

        for (auto map_name : map_names)
            if (stricmp(old_name, map_name.old_name) == 0)
            {
                name = map_name.new_name;
                if (!value)
                    value = map_name.default_value;
                break;
            }

        if (!name)
            return false;
    }

    out.emplace_back(name, value);
    return true;
}

//------------------------------------------------------------------------------
static bool save_internal(const char* file, bool migrating);

//------------------------------------------------------------------------------
bool load(const char* file, const char* default_file)
{
#ifdef DEBUG
    s_ever_loaded = true;
#endif

    if (file)
    {
        if (!g_last_file)
            g_last_file = new str_moveable;
        if (file != g_last_file->c_str())
            *g_last_file = file;
    }

    load_custom_defaults(default_file);
    get_loaded_map().clear();

    // Reset settings to default.
    for (auto iter = settings::first(); auto* next = iter.next();)
        next->set();

    if (!file)
        return true;

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

    load_internal(in, [migrating](const char* name, const char* value, const char* comment)
    {
        // Migrate old setting.
        if (migrating)
        {
            std::vector<settings::setting_name_value> migrated_settings;
            if (migrate_setting(name, value, migrated_settings))
            {
                for (const auto& pair : migrated_settings)
                    set_setting(pair.name.c_str(), pair.value.c_str());
            }
            return;
        }

        // Find the setting and set its value.
        set_setting(name, value, comment);
    });

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
    // Make sure the directory exists, since %CLINK_SETTINGS% may point to a
    // directory that does not yet exist.
    str<> parent(file);
    path::to_parent(parent, nullptr);
    os::make_dir(parent.c_str());

    // Open settings file.  When migrating, fail if the file already exists, so
    // that if multiple concurrent migrations occur only one of them writes the
    // new settings file.
    FILE* out = fopen(file, migrating ? "wtx" : "wt");
    if (out == nullptr)
        return false;

    // Clear the saved flag so we can track which ones have been saved so far.
    for (auto iter : get_loaded_map())
        iter.second.saved = false;

    // Iterate over each setting and write it out to the file.
    for (auto i : get_map())
    {
        setting* iter = i.second;
        auto loaded = get_loaded_map().find(iter->get_name());
        if (loaded != get_loaded_map().end())
            loaded->second.saved = true;

        // Only write out settings that have been explicitly set.
        if (!iter->is_saveable())
            continue;

        fprintf(out, "# name: %s\n", iter->get_short_desc());

        // Write out the setting's type.
        int32 type = iter->get_type();
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
    for (const auto& iter : get_loaded_map())
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

//------------------------------------------------------------------------------
bool parse_ini(const char* file, std::vector<setting_name_value>& out)
{
    // Open the file.
    FILE* in = fopen(file, "rb");
    if (in == nullptr)
        return false;

    out.clear();
    return parse_ini(in, out);
}

//------------------------------------------------------------------------------
void overlay(const std::vector<setting_name_value>& overlay)
{
    for (const auto& o : overlay)
    {
        if (o.clear)
            clear_setting(o.name.c_str());
        else
            set_setting(o.name.c_str(), o.value.c_str());
    }
}

//------------------------------------------------------------------------------
#ifdef DEBUG
bool get_ever_loaded()
{
    return s_ever_loaded;
}

void TEST_set_ever_loaded()
{
    s_ever_loaded = true;
}
#endif

//------------------------------------------------------------------------------
void get_settings_file(str_base& out)
{
    out.clear();
    if (g_last_file)
        out.concat(g_last_file->c_str(), g_last_file->length());
}

//------------------------------------------------------------------------------
class rollback_settings_values
{
public:
    rollback_settings_values()
    {
        str_moveable value;
        for (auto& i : get_map())
        {
            setting* iter = i.second;
            iter->get(value);
            m_restore.emplace(iter->get_name(), std::move(value));
        }
    }
    ~rollback_settings_values()
    {
        for (auto& r : m_restore)
        {
            setting* iter = settings::find(r.first.c_str());
            assert(iter); // (It was there a moment ago!)
            if (iter)
                iter->set(r.second.c_str());
        }
    }
private:
    std::map<std::string, str_moveable> m_restore;
};

//------------------------------------------------------------------------------
bool sandboxed_set_setting(const char* name, const char* value)
{
    if (!g_last_file)
        return false;
    const char* file = g_last_file->c_str();

    // Swap real settings data structures with new temporary versions.
    loaded_settings_map tmp_loaded_map;
    rollback_settings_values rb_settings;
    rollback<loaded_settings_map*> rb_loaded(g_loaded_settings, &tmp_loaded_map);

    // Load settings.
    return (load(file) &&
            set_setting(name, value) &&
            save(file));
}

//------------------------------------------------------------------------------
bool sandboxed_overlay(const std::vector<setting_name_value>& overlay)
{
    if (overlay.empty())
        return false;

    if (!g_last_file)
        return false;
    const char* file = g_last_file->c_str();

    // Swap real settings data structures with new temporary versions.
    loaded_settings_map tmp_loaded_map;
    rollback_settings_values rb_settings;
    rollback<loaded_settings_map*> rb_loaded(g_loaded_settings, &tmp_loaded_map);

    if (!load(file))
        return false;

    for (const auto& o : overlay)
        set_setting(o.name.c_str(), o.value.c_str());

    if (!save(file))
        return false;

    return true;
}

//------------------------------------------------------------------------------
static bool imatch3(const str<16>& a, const char* b)
{
    return _strnicmp(a.c_str(), b, 3) == 0;
}

//------------------------------------------------------------------------------
static int32 parsehexdigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

//------------------------------------------------------------------------------
static int32 parsehex(const str<16>& a)
{
    if (*a.c_str() != '#')
        return -1;

    const uint32 len = a.length();
    if (len != 4 && len != 7)
        return -1;

    const bool double_digits = (len == 4);
    int32 value = 0;
    for (const char* s = a.c_str() + 1; *s; ++s)
    {
        int32 h = parsehexdigit(*s);
        if (h < 0)
            return -1;
        value <<= 4;
        value += h;
        if (double_digits)
        {
            value <<= 4;
            value += h;
        }
    }

    return value;
}

//------------------------------------------------------------------------------
static const char* const color_names[] = { "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white" };

//------------------------------------------------------------------------------
bool parse_color(const char* value, str_base& out)
{
    if (!value || !*value)
    {
        out.clear();
        return true;
    }

    str<> code;
    str<16> token;
    int32 fg = -1;
    int32 bg = -1;
    bool fg_hex = false;
    bool bg_hex = false;
    int32 bold = -1;
    int32 bright = -1;
    int32 underline = -1;
    bool italic = false;
    bool reverse = false;
    int32* pcolor = &fg;
    bool* phex = &fg_hex;
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
            out = code.c_str();
            return true;
        }

        first_part = false;

        if (strcmpi(token.c_str(), "on") == 0)
        {
            if (pcolor == &bg) return false; // can't use "on" more than once
            if (!*phex && bright > 0)
                fg += 8;
            pcolor = &bg;
            phex = &bg_hex;
            bright = -1;
            saw_default = false;
            continue;
        }

        if (imatch3(token, "normal") || imatch3(token, "default"))
        {
            if (*pcolor >= 0) return false; // disallow combinations
            if (saw_default) return false;
            *pcolor = -1;
            *phex = false;
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
            if (*phex) return false; // disallow combinations
            if (bright >= 0) return false; // disallow combinations
            bright = imatch3(token, "bright");
            continue;
        }

        if (imatch3(token, "underline") || imatch3(token, "nounderline"))
        {
            if (pcolor == &bg) return false; // only applies to fg
            if (underline >= 0) return false; // disallow combinations
            // "nounderline" is still recognized for backward compatibility,
            // but it has no effect.
            underline = imatch3(token, "underline");
            continue;
        }

        if (imatch3(token, "italic"))
        {
            if (pcolor == &bg) return false; // only applies to fg
            if (italic) return false; // disallow combinations
            italic = true;
            continue;
        }

        if (imatch3(token, "reverse"))
        {
            if (reverse) return false; // disallow combinations
            reverse = true;
            continue;
        }

        const int32 h = parsehex(token);
        if (h >= 0)
        {
            if (*pcolor >= 0) return false; // disallow combinations
            if (bright >= 0) return false; // disallow combinations
            *pcolor = h;
            *phex = true;
            continue;
        }

        int32 i;
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

    if (*pcolor >= 0 && !*phex && bright > 0)
        (*pcolor) += 8;

    code = "0";

    if (reverse)
        code << ";7";

    if (bold > 0)
        code << ";1";
    else if (bold == 0)
        code << ";22";

    if (italic)
        code << ";3";

    if (underline > 0)
        code << ";4";

    if (fg >= 0)
    {
        if (fg_hex)
        {
            char r[10];
            char g[10];
            char b[10];
            itoa(fg & 0xff, b, 10);
            fg >>= 8;
            itoa(fg & 0xff, g, 10);
            fg >>= 8;
            itoa(fg, r, 10);
            code << ";38;2;" << r << ";" << g << ";" << b;
        }
        else
        {
            if (fg >= 8)
                fg += 60 - 8;
            fg += 30;
            char buf[10];
            itoa(fg, buf, 10);
            code << ";" << buf;
        }
    }

    if (bg >= 0)
    {
        if (bg_hex)
        {
            char r[10];
            char g[10];
            char b[10];
            itoa(bg & 0xff, b, 10);
            bg >>= 8;
            itoa(bg & 0xff, g, 10);
            bg >>= 8;
            itoa(bg, r, 10);
            code << ";48;2;" << r << ";" << g << ";" << b;
        }
        else
        {
            if (bg >= 8)
                bg += 60 - 8;
            bg += 40;
            char buf[10];
            itoa(bg, buf, 10);
            code << ";" << buf;
        }
    }

    out = code.c_str();
    return true;
}

//------------------------------------------------------------------------------
static int32 int_from_str_iter(const str_iter& iter)
{
    int32 x = 0;
    int32 c = iter.length();
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
static bool parse24bit(str_tokeniser& parts, str_iter& part, char* buf)
{
    if (!parts.next(part)) return false;
    if (int_from_str_iter(part) != 2) return false;

    if (!parts.next(part)) return false;
    const int32 r = int_from_str_iter(part);
    if (r < 0 || r > 255) return false;

    if (!parts.next(part)) return false;
    const int32 g = int_from_str_iter(part);
    if (g < 0 || g > 255) return false;

    if (!parts.next(part)) return false;
    const int32 b = int_from_str_iter(part);
    if (b < 0 || b > 255) return false;

    sprintf(buf, "#%02.2X%02.2X%02.2X", r, g, b);
    return true;
}

//------------------------------------------------------------------------------
static bool strip_if_ends_with(str_base& s, const char* suffix, uint32 len)
{
    if (s.length() > len && strcmp(s.c_str() + s.length() - len, suffix) == 0)
    {
        s.truncate(s.length() - len);
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
void format_color(const char* const color, str_base& out, const bool compat)
{
    out.clear();
    if (!color || !*color)
        return;

    enum { reset_token, reverse_token, bold_token, italic_token, underline_token, fg_token, bg_token, nomore_tokens };
    int32 expected = reset_token;
    str_iter part;
    str_tokeniser parts(color, ";");

    bool any_fg = false;
    while (parts.next(part))
    {
        int32 x = int_from_str_iter(part);
        if (x < 0)
        {
nope:
            out.clear();
            out << "sgr " << color;
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
        else if (x == 7)
        {
            if (expected > reverse_token) goto nope;
            expected = reverse_token + 1;
            out << "reverse ";
        }
        else if (x == 1 || x == 22)
        {
            if (expected > bold_token) goto nope;
            expected = bold_token + 1;
            out << ((x == 1) ? "bold " : "nobold ");
        }
        else if (x == 3)
        {
            if (expected > italic_token) goto nope;
            expected = italic_token + 1;
            out << "italic ";
        }
        else if (x == 4)
        {
            if (expected > underline_token) goto nope;
            expected = underline_token + 1;
            out << "underline ";
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
            any_fg = true;
        }
        else if (x == 38 && !compat)
        {
            if (expected > fg_token) goto nope;
            expected = fg_token + 1;
            char buf[10];
            if (!parse24bit(parts, part, buf)) goto nope;
            out << buf << " ";
            any_fg = true;
        }
        else if (x == 39)
        {
            if (expected > fg_token) goto nope;
            expected = fg_token + 1;
            out << "default ";
            any_fg = true;
        }
        else if ((x >= 40 && x < 48) || (x >= 100 && x < 108))
        {
            if (expected > bg_token) goto nope;
            expected = bg_token + 1;
            if (!any_fg)
                out << "default ";
            out << "on ";
            x -= 10;
            if (x >= 90)
            {
                out << "bright ";
                x -= 60;
            }
            out << color_names[x - 30] << " ";
        }
        else if (x == 48 && !compat)
        {
            if (expected > bg_token) goto nope;
            expected = bg_token + 1;
            char buf[10];
            if (!parse24bit(parts, part, buf)) goto nope;
            if (!any_fg)
                out << "default ";
            out << "on ";
            out << buf << " ";
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
    assert(strlen(name) == m_name.length());
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
    const auto loaded = get_loaded_map().find(name);
    if (loaded == get_loaded_map().end())
        return nullptr;
    return loaded->second.value.c_str();
}

//------------------------------------------------------------------------------
const char* setting::get_custom_default() const
{
    const auto custom_default = get_custom_default_map().find(m_name.c_str());
    if (custom_default == get_custom_default_map().end())
        return nullptr;
    return custom_default->second.value.c_str();
}

//------------------------------------------------------------------------------
void setting::set_source(const char* source)
{
    assert(m_source.empty());
    m_source = source;
}

//------------------------------------------------------------------------------
const char* setting::get_source() const
{
    return m_source.c_str();
}



//------------------------------------------------------------------------------
template <> bool setting_impl<bool>::parse(const char* value, store<bool>& out)
{
    // In issue 372 someone tried to manually modify the clink_settings file,
    // and they accidentally added a trailing space, which Clink did not expect.
    // Manually modifying the clink_settings file is not supported.  However,
    // this particular usage mistake is easy to accommodate by trimming
    // whitespace, and it is less costly to make a change that it is to
    // investigate support questions caused by an invisible character which is
    // present due to a side effect of CMD batch script syntax.
    str<16> tmp(value);
    tmp.trim();
    value = tmp.c_str();

    if (stricmp(value, "true") == 0)  { out.value = 1; return true; }
    if (stricmp(value, "false") == 0) { out.value = 0; return true; }

    if (stricmp(value, "on") == 0)    { out.value = 1; return true; }
    if (stricmp(value, "off") == 0)   { out.value = 0; return true; }

    if (stricmp(value, "yes") == 0)   { out.value = 1; return true; }
    if (stricmp(value, "no") == 0)    { out.value = 0; return true; }

    if (*value >= '0' && *value <= '9')
    {
        out.value = !!atoi(value);
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
template <> bool setting_impl<int32>::parse(const char* value, store<int32>& out)
{
    if ((*value < '0' || *value > '9') && *value != '-')
        return false;

    out.value = atoi(value);
    return true;
}

//------------------------------------------------------------------------------
template <> bool setting_impl<const char*>::parse(const char* value, store<const char*>& out)
{
    out.value = value;
    return true;
}



//------------------------------------------------------------------------------
template <> void setting_impl<bool>::get(str_base& out) const
{
    out = m_store.value ? "True" : "False";
}

//------------------------------------------------------------------------------
template <> void setting_impl<int32>::get(str_base& out) const
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
    int32 default_value)
: setting_enum(name, short_desc, nullptr, options, default_value)
{
}

//------------------------------------------------------------------------------
setting_enum::setting_enum(
    const char* name,
    const char* short_desc,
    const char* long_desc,
    const char* options,
    int32 default_value)
: setting_impl<int32>(name, short_desc, long_desc, default_value)
, m_options(options)
{
    m_type = type_enum;
}

//------------------------------------------------------------------------------
bool setting_enum::parse(const char* value, store<int32>& out)
{
    int32 by_int = -1;
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

    int32 i = 0;
    for (const char* option = m_options.c_str(); *option; ++i)
    {
        const char* next = next_option(option);

        int32 option_len = int32(next - option);
        if (*next)
            --option_len;

        bool match = (by_int == 0);
        if (!match)
        {
            str_compare_scope _(str_compare_scope::caseless, false);
            str_iter oi(option, option_len);
            str_iter vi(value);
            match = (str_compare(oi, vi) < 0);
        }

        if (match)
        {
            out.value = i;
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
    int32 index = m_store.value;
    if (index < 0)
        return;

    const char* option = m_options.c_str();
    for (int32 i = 0; i < index && *option; ++i)
        option = next_option(option);

    if (*option)
    {
        const char* next = next_option(option);
        if (*next)
            --next;

        out.clear();
        out.concat(option, int32(next - option));
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
bool setting_color::parse(const char* value, store<const char*>& out)
{
    return settings::parse_color(value, out.value);
}

//------------------------------------------------------------------------------
void setting_color::set()
{
    const char* custom_default = get_custom_default();
    if (!custom_default || !parse(custom_default, m_store))
        parse(static_cast<const char*>(m_default), m_store);
    m_save = !is_default();
}

//------------------------------------------------------------------------------
void setting_color::get_descriptive(str_base& out, bool compat) const
{
    str<> tmp;
    get(tmp);
    settings::format_color(tmp.c_str(), out, compat);
}
