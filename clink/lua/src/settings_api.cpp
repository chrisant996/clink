// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/settings.h>

#include <new.h>

//------------------------------------------------------------------------------
/// -name:  settings.get
/// -arg:   name:string
/// -ret:   boolean or string or integer
/// Returns the current value of the <span class="arg">name</span> Clink
/// setting.
static int get(lua_State* state)
{
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

    const char* key = lua_tostring(state, 1);
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
            setting->get(value);
            lua_pushstring(state, value.c_str());
        }
        break;
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  settings.set
/// -arg:   name:string
/// -arg:   value:string
/// -ret:   boolean
/// Sets the <span class="arg">name</span> Clink setting to
/// <span class="arg">value</span> and returns whether it was successful.
static int set(lua_State* state)
{
    if (lua_gettop(state) < 2 || !lua_isstring(state, 1))
        return 0;

    const char* key = lua_tostring(state, 1);
    setting* setting = settings::find(key);
    if (setting == nullptr)
        return 0;

    const char* value = lua_tostring(state, 2);
    bool ok = setting->set(value);

    lua_pushboolean(state, ok == true);
    return 1;
}

//------------------------------------------------------------------------------
template <typename S, typename... V> void add_impl(lua_State* state, V... value)
{
    const char* name = lua_tostring(state, 1);
    const char* short_desc = (lua_gettop(state) > 2) ? lua_tostring(state, 3) : "";
    const char* long_desc = (lua_gettop(state) > 3) ? lua_tostring(state, 4) : "";

    void* addr = lua_newuserdata(state, sizeof(S));
    new (addr) S(name, short_desc, long_desc, value...);

    // Apply the value read during load, if any had been saved.
    ((S*)addr)->deferred_load();

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
/// -arg:   name:string
/// -arg:   default:...
/// -arg:   [short_desc:string]
/// -arg:   [long_desc:string]
/// -ret:   boolean
/// -show:  settings.add("myscript.myabc", true, "Boolean setting")
/// -show:  settings.add("myscript.mydef", 100, "Number setting")
/// -show:  settings.add("myscript.myghi", "abc", "String setting")
/// -show:  settings.add("myscript.myjkl", {"x","y","z"}, "Enum setting")
/// -show:  settings.add("color.mymno", "bright magenta", "Color setting")
/// Adds a setting to the list of Clink settings and includes it in
/// <code>clink set</code>.  The new setting is named
/// <span class="arg">name</span> and has a default value
/// <span class="arg">default</span> when the setting isn't explicitly set.
///
/// The type of <span class="arg">default</span> determines what kind of setting
/// is added:  boolean, integer, and string values add the corresponding setting
/// type.  Or if the type is table then an enum setting is added:  the table
/// defines the accepted values, and the first value is the default value.  Or
/// if it's a string type and the name starts with "color." then a color setting
/// is added.
///
/// <span class="arg">name</span> can't be more than 31 characters.<br/>
/// <span class="arg">short_desc</span> is an optional quick summary description
/// and can't be more than 47 characters.<br/>
/// <span class="arg">long_desc</span> is an optional long description.
static int add(lua_State* state)
{
    if (lua_gettop(state) < 2 || !lua_isstring(state, 1))
    {
        lua_pushboolean(state, 0);
        return 1;
    }

    const char* name = lua_tostring(state, 1);
    if (settings::find(name))
        return luaL_error(state, "Setting '%s' already exists", name);

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
        lua_pushboolean(state, 0);
        return 1;
    }

    lua_pushboolean(state, 1);
    return 1;
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
