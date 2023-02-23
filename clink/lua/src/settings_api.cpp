// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/settings.h>
#include <core/str_tokeniser.h>
#include <core/debugheap.h>

#include <new.h>

//------------------------------------------------------------------------------
extern setting_bool g_lua_strict;



//------------------------------------------------------------------------------
/// -name:  settings.get
/// -ver:   1.0.0
/// -arg:   name:string
/// -arg:   [descriptive:boolean]
/// -ret:   boolean | string | integer | nil
/// Returns the current value of the <span class="arg">name</span> Clink
/// setting or nil if the setting does not exist.
///
/// The return type corresponds to the setting type:
/// <ul>
/// <li>Boolean settings return a boolean value.
/// <li>Integer settings return an integer value.
/// <li>Enum settings return an integer value corresponding to a position in the
/// setting's table of accepted values.  The first position is 0, the second
/// position is 1, etc.
/// <li>String settings return a string.
/// <li>Color settings return a string.
/// </ul>
///
/// Color settings normally return the ANSI color code, suitable for use in an
/// ANSI escape sequence.  If the optional <span class="arg">descriptive</span>
/// parameter is true then the friendly color name is returned.
/// -show:  print(settings.get("color.doskey"))         -- Can print "1;36"
/// -show:  print(settings.get("color.doskey", true))   -- Can print "bold cyan"
static int get(lua_State* state)
{
    const char* key = checkstring(state, 1);
    if (!key)
        return 0;

    const setting* setting = settings::find(key);
    if (setting == nullptr)
        return 0;

    int type = setting->get_type();
    switch (type)
    {
    case setting::type_bool:
        {
            bool value = ((setting_bool*)setting)->get();
            lua_pushboolean(state, value == true);
        }
        break;

    case setting::type_int:
        {
            int value = ((setting_int*)setting)->get();
            lua_pushinteger(state, value);
        }
        break;

    default:
        {
            str<> value;
            if (lua_isboolean(state, 2) && lua_toboolean(state, 2))
                setting->get_descriptive(value);
            else
                setting->get(value);
            lua_pushlstring(state, value.c_str(), value.length());
        }
        break;
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  settings.set
/// -ver:   1.0.0
/// -arg:   name:string
/// -arg:   value:...
/// -ret:   boolean
/// Sets the <span class="arg">name</span> Clink setting to
/// <span class="arg">value</span> and returns whether it was successful.
///
/// The type of <span class="arg">value</span> depends on the type of the
/// setting.  Some automatic type conversions are performed when appropriate.
///
/// <ul>
/// <li>Boolean settings convert string values
/// <code>"true"</code>/<code>"false"</code>,
/// <code>"yes"</code>/<code>"no"</code>, and
/// <code>"on"</code>/<code>"off"</code> into a boolean
/// <code>true</code>/<code>false</code>.  Any numeric values are converted to
/// <code>true</code>, even <code>0</code> (Lua considers 0 to be true, unlike
/// some other languages).
/// <li>Integer settings convert string values starting with a digit or minus
/// sign into an integer value.
/// <li>Enum settings convert the allowed string values into an integer value
/// corresponding to the position in the table of allowed values.  The first
/// position is 0, the second position is 1, etc.
/// <li>String settings convert boolean or number values into a corresponding
/// string value.
/// <li>Color settings convert boolean or number values into a corresponding
/// string value.
/// </ul>
///
/// Note: Beginning in Clink v1.2.31 this updates the settings file.  Prior to
/// that, it was necessary to separately use <code>clink set</code> to update
/// the settings file.
static int set(lua_State* state)
{
    const char* key = checkstring(state, 1);
    if (!key)
        return 0;

    setting* setting = settings::find(key);
    if (setting == nullptr)
        return 0;

    const char* value;
    if (lua_isboolean(state, 2))
        value = lua_toboolean(state, 2) ? "true" : "false";
    else
        value = checkstring(state, 2);
    if (!value)
        return 0;

    // Update the settings file and the in-memory setting.
    bool ok = setting->set(value);
    if (ok)
        ok = settings::sandboxed_set_setting(key, value);

    lua_pushboolean(state, ok == true);
    return 1;
}

//------------------------------------------------------------------------------
template <typename S, typename... V> void add_impl(lua_State* state, V... value)
{
    const char* name = checkstring(state, 1);
    const char* short_desc = (lua_gettop(state) > 2) ? checkstring(state, 3) : "";
    const char* long_desc = (lua_gettop(state) > 3) ? checkstring(state, 4) : "";

    void* addr = lua_newuserdata(state, sizeof(S));

    // Initialize the definition and apply any value that was read during load.
    dbg_snapshot_heap(snapshot);
    new (addr) S(name, short_desc, long_desc, value...);
    ((S*)addr)->deferred_load();
    dbg_ignore_since_snapshot(snapshot, "Settings");

    if (luaL_newmetatable(state, "settings_mt"))
    {
        lua_pushliteral(state, "__gc");
        lua_pushcfunction(state, [](lua_State* state) -> int {
            setting* s = (setting*)lua_touserdata(state, -1);
            s->~setting();
            return 0;
        });
        lua_rawset(state, -3);
    }

    lua_setmetatable(state, -2);
    luaL_ref(state, LUA_REGISTRYINDEX);
}

//------------------------------------------------------------------------------
/// -name:  settings.add
/// -ver:   1.0.0
/// -arg:   name:string
/// -arg:   default:...
/// -arg:   [short_desc:string]
/// -arg:   [long_desc:string]
/// -ret:   boolean
/// Adds a setting to the list of Clink settings and includes it in
/// <code>clink set</code>.  The new setting is named
/// <span class="arg">name</span> and has a default value
/// <span class="arg">default</span> when the setting isn't explicitly set.
///
/// The type of <span class="arg">default</span> determines what kind of setting
/// is added:
/// <ul>
/// <li>Boolean; a boolean value adds a boolean setting.
/// <li>Integer; an integer value adds an integer setting.
/// <li>Enum; a table adds an enum setting.  The table defines the accepted
/// string values, and the first value is the default value.  The setting has an
/// integer value which corresponds to the position in the table of accepted
/// values.  The first position is 0, the second position is 1, etc.
/// <li>String; a string value adds a string setting.
/// <li>Color; when <span class="arg">name</span> begins with
/// <code>"color."</code> then a string value adds a color setting.
/// </ul>
///
/// <span class="arg">name</span> can't be more than 32 characters.
///
/// <span class="arg">short_desc</span> is an optional quick summary description
/// and can't be more than 48 characters.
///
/// <span class="arg">long_desc</span> is an optional long description.
/// -show:  settings.add("myscript.myabc", true, "Boolean setting")
/// -show:  settings.add("myscript.mydef", 100, "Number setting")
/// -show:  settings.add("myscript.myghi", "abc", "String setting")
/// -show:  settings.add("myscript.myjkl", {"x","y","z"}, "Enum setting")
/// -show:  settings.add("color.mymno", "bright magenta", "Color setting")
static int add(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (!name)
        return 0;

    if (settings::find(name))
        return luaL_error(state, "setting " LUA_QS " already exists", name);

    switch (lua_type(state, 2))
    {
    case LUA_TNUMBER:
        add_impl<setting_int>(state, int(lua_tointeger(state, 2)));
        break;

    case LUA_TBOOLEAN:
        add_impl<setting_bool>(state, lua_toboolean(state, 2) == 1);
        break;

    case LUA_TSTRING:
        if (_strnicmp(name, "color.", 6) == 0)
            add_impl<setting_color>(state, (const char *)lua_tostring(state, 2));
        else
            add_impl<setting_str>(state, (const char *)lua_tostring(state, 2));
        break;

    case LUA_TTABLE:
        {
            str<256> options;
            for (int i = 0, n = int(lua_rawlen(state, 2)); i < n; ++i)
            {
                lua_rawgeti(state, 2, i + 1);
                if (const char* option = lua_tostring(state, -1))
                {
                    if (i) options << ",";
                    options << option;
                }
                lua_pop(state, 1);
            }

            add_impl<setting_enum>(state, options.c_str(), 0);
        }
        break;

    default:
        if (g_lua_strict.get())
            luaL_argerror(state, 2, "must be number, boolean, string, or table of strings");
        else
            lua_pushboolean(state, 0);
        return 1;
    }

    lua_pushboolean(state, 1);
    return 1;
}

//------------------------------------------------------------------------------
static void table_add_string(lua_State* state, const char* s, int& count)
{
    lua_pushstring(state, s);
    lua_rawseti(state, -2, ++count);
}

//------------------------------------------------------------------------------
static void push_setting_values_table(lua_State* state, const setting* setting, bool classify)
{
    lua_createtable(state, 0, 1);

    int count = 0;
    switch (setting->get_type())
    {
    case setting::type_int:
    case setting::type_string:
        break;

    case setting::type_bool:
        table_add_string(state, "true", count);
        table_add_string(state, "false", count);
        if (classify)
        {
            table_add_string(state, "1", count);
            table_add_string(state, "0", count);
            table_add_string(state, "yes", count);
            table_add_string(state, "no", count);
            table_add_string(state, "on", count);
            table_add_string(state, "off", count);
        }
        break;

    case setting::type_enum:
        {
            const char* options = ((const setting_enum*)setting)->get_options();
            str_tokeniser tokens(options, ",");
            const char* start;
            int length;
            str<> tmp;
            while (tokens.next(start, length))
            {
                tmp.clear();
                tmp.concat(start, length);
                table_add_string(state, tmp.c_str(), count);
            }
        }
        break;

    case setting::type_color:
        {
            static const char* const color_keywords[] =
            {
                "bold", "nobold", "underline", "nounderline",
                "bright", "default", "normal", "on",
                "black", "red", "green", "yellow",
                "blue", "cyan", "magenta", "white",
                "sgr",
            };

            for (auto keyword : color_keywords)
                table_add_string(state, keyword, count);
        }
        break;
    }

    table_add_string(state, "clear", count);
}

//------------------------------------------------------------------------------
// Undocumented, because it's only needed internally.
static int list(lua_State* state)
{
    if (lua_gettop(state) < 1)
    {
        lua_createtable(state, 0, 1);

        int count = 0;
        for (setting_iter iter = settings::first(); const setting* setting = iter.next();)
        {
            lua_createtable(state, 0, 2);

            lua_pushliteral(state, "match");
            lua_pushstring(state, setting->get_name());
            lua_rawset(state, -3);

            lua_pushliteral(state, "description");
            lua_pushstring(state, setting->get_short_desc());
            lua_rawset(state, -3);

            lua_rawseti(state, -2, ++count);
        }

        return 1;
    }

    if (lua_isstring(state, 1))
    {
        static const char* const type_names[] = { "unknown", "int", "boolean", "string", "enum", "color" };
        static_assert(sizeof_array(type_names) == int(setting::type_e::type_max), "type_names and setting::type_e::type_max disagree");

        const char *name = lua_tostring(state, 1);
        bool classify = lua_isboolean(state, 2) && lua_toboolean(state, 2);

        const setting* setting = settings::find(name);
        if (setting == nullptr)
            return 0;

        lua_createtable(state, 0, 3);

        lua_pushliteral(state, "name");
        lua_pushstring(state, name);
        lua_rawset(state, -3);

        lua_pushliteral(state, "type");
        lua_pushstring(state, type_names[int(setting->get_type())]);
        lua_rawset(state, -3);

        lua_pushliteral(state, "values");
        push_setting_values_table(state, setting, classify);
        lua_rawset(state, -3);

        return 1;
    }

    return 0;
}

//------------------------------------------------------------------------------
void settings_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "get",    &get },
        { "set",    &set },
        { "add",    &add },
        { "list",   &list },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "settings");
}

//----------------------------------------------------------------------------
// Clink 0.4.8 API compatibility!
int get_clink_setting(lua_State* state)
{
    return get(state);
}
