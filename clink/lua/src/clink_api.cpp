// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "lua_input_idle.h"
#include "line_state_lua.h"
#include "line_states_lua.h"
#include "prompt.h"
#include "async_lua_task.h"
#include "command_link_dialog.h"
#include "sessionstream.h"
#include "../../app/src/version.h" // Ugh.

#ifdef CLINK_USE_LUA_EDITOR_TESTER
#include "lua_editor_tester.h"
#endif

#include <core/base.h>
#include <core/os.h>
#include <core/cwd_restorer.h>
#include <core/str_compare.h>
#include <core/str_transform.h>
#include <core/str_unordered_set.h>
#include <core/settings.h>
#include <core/linear_allocator.h>
#include <core/callstack.h>
#include <core/debugheap.h>
#include <lib/popup.h>
#include <lib/cmd_tokenisers.h>
#include <lib/reclassify.h>
#include <lib/recognizer.h>
#include <lib/matches_lookaside.h>
#include <lib/line_editor_integration.h>
#include <lib/rl_integration.h>
#include <lib/suggestions.h>
#include <lib/slash_translation.h>
#include <lib/host_callbacks.h>
#include <terminal/terminal_helpers.h>
#include <terminal/printer.h>
#include <terminal/screen_buffer.h>
#include <shellapi.h>

extern "C" {
#include <lua.h>
#include <readline/history.h>
}

#include <share.h>
#include <mutex>



//------------------------------------------------------------------------------
static bool s_test_harness = false;
void set_test_harness() { s_test_harness = true; }



//------------------------------------------------------------------------------
extern setting_enum g_dupe_mode;
extern setting_bool g_lua_breakonerror;
extern setting_bool g_match_wild;

#ifdef _WIN64
static const char c_uninstall_key[] = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#else
static const char c_uninstall_key[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
#endif

//------------------------------------------------------------------------------
const uint32 c_snooze_duration = 22 * 60 * 60;  // Effectively one day, but avoids the threshold creeping forward over time.
static HANDLE s_hMutex = 0;

//------------------------------------------------------------------------------
static bool acquire_updater_mutex()
{
    if (s_hMutex)
        return true;

    wstr<280> mod;
    const DWORD mod_len = GetModuleFileNameW(nullptr, mod.data(), mod.size());
    if (!mod_len || mod_len >= mod.size())
        return false;

    // Only allow the standalone clink exe to acquire the mutex, to avoid
    // blocking inside cmd.exe.
    str<16> name(path::get_name(mod.c_str()));
    if (_strnicmp("clink_", name.c_str(), 6) != 0)
    {
        assert(false);
        return false;
    }

    // Acquire a shared named mutex to synchronize all update attempts.  Since
    // this can never happen inside cmd.exe, it's reasonable to "leak" the
    // mutex so that when this "clink update" process finishes, the next
    // waiting one can continue.  This greatly simplifies managing the
    // acquisition and release of the mutex.
    HANDLE hMutex = CreateMutex(nullptr, false, "clink_autoupdate_global_serializer");
    if (hMutex)
    {
        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0)
        {
            CloseHandle(hMutex);
            return false;
        }
        s_hMutex = hMutex;
    }

    return true;
}

//------------------------------------------------------------------------------
static void release_updater_mutex()
{
    if (s_hMutex)
    {
        ReleaseMutex(s_hMutex);
        CloseHandle(s_hMutex);
        s_hMutex = 0;
    }
}

//------------------------------------------------------------------------------
static bool make_key_value_names(const char* subkey, wstr_base& keyname, wstr_base& valname)
{
    str<280> tmp1;
    if (!os::get_alias("clink", tmp1))
        return false;

    str<280> tmp2;
    if (!path::get_directory(tmp1.c_str(), tmp2))
        return false;

    if (!tmp1.format("Software\\Clink\\%s", subkey))
        return false;

    keyname = tmp1.c_str();
    valname = tmp2.c_str();
    return true;
}

//------------------------------------------------------------------------------
static int32 encode_version(const char* str)
{
    int32 version = 0;
    if (str && 'v' == *(str++))
    {
        char* end;
        const int32 major = strtol(str, &end, 10);
        if (end && *end == '.')
        {
            version = major;
            const int32 minor = strtol(end + 1, &end, 10);
            if (end && *end == '.')
            {
                version *= 1000;
                version += minor;
                const int32 patch = strtol(end + 1, &end, 10);
                if (end && (*end == '.' || !*end))
                {
                    version *= 10000;
                    version += patch;
                    return version;
                }
            }
        }

    }
    return 0;
}

//------------------------------------------------------------------------------
static bool is_update_skipped(const char* new_ver, str_base* reg_ver=nullptr)
{
    const int32 candidate = encode_version(new_ver);
    if (!candidate)
        return false;

    wstr<> keyname;
    wstr<> valname;
    if (!make_key_value_names("SkipUpdate", keyname, valname))
        return false;

    DWORD type;
    WCHAR buffer[280];
    DWORD size = sizeof(buffer);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, keyname.c_str(), valname.c_str(), RRF_RT_REG_SZ, &type, buffer, &size);
    if (status != ERROR_SUCCESS || type != REG_SZ)
        return false;

    str<16> tmp;
    if (!reg_ver)
        reg_ver = &tmp;
    *reg_ver = buffer;

    const int32 skip = encode_version(reg_ver->c_str());
    if (!candidate || !skip || skip < candidate)
        return false;

    return true;
}

//------------------------------------------------------------------------------
static bool is_update_prompt_snoozed()
{
    wstr<> keyname;
    wstr<> valname;
    if (make_key_value_names("SnoozeUpdatePrompt", keyname, valname))
    {
        DWORD type;
        WCHAR buffer[280];
        DWORD size = sizeof(buffer);
        LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, keyname.c_str(), valname.c_str(), RRF_RT_REG_SZ, &type, buffer, &size);
        if (status == ERROR_SUCCESS && type == REG_SZ)
        {
            str<16> snooze(buffer);
            time_t until = atoi(snooze.c_str());
            time_t now = time(nullptr);
            if (until > 0 && now > 0 && now < until)
                return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
static void snooze_update_prompt()
{
    wstr<> keyname;
    wstr<> valname;
    if (!make_key_value_names("SnoozeUpdatePrompt", keyname, valname))
        return;

    HKEY hkey;
    DWORD dwDisposition;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, keyname.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, nullptr, &hkey, &dwDisposition);
    if (status != ERROR_SUCCESS)
        return;

    wstr<> snooze;
    time_t until = time(nullptr) + c_snooze_duration;
    snooze.format(L"%u", until);
    status = RegSetValueExW(hkey, valname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(snooze.c_str()), snooze.length() * sizeof(*snooze.c_str()));

    RegCloseKey(hkey);
}

//------------------------------------------------------------------------------
static void skip_update(const char* ver)
{
    wstr<> keyname;
    wstr<> valname;
    if (!make_key_value_names("SkipUpdate", keyname, valname))
        return;

    HKEY hkey;
    DWORD dwDisposition;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, keyname.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, nullptr, &hkey, &dwDisposition);
    if (status != ERROR_SUCCESS)
        return;

    wstr<> wver(ver);
    status = RegSetValueExW(hkey, valname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(wver.c_str()), wver.length() * sizeof(*wver.c_str()));

    RegCloseKey(hkey);
}



//------------------------------------------------------------------------------
/// -name:  clink.print
/// -ver:   1.2.11
/// -arg:   ...
/// This works like
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-print">print()</a>,
/// but this supports ANSI escape codes and Unicode.
///
/// If the special value <code>NONL</code> is included anywhere in the argument
/// list then the usual trailing newline is omitted.  This can sometimes be
/// useful particularly when printing certain ANSI escape codes.
///
/// <strong>Note:</strong>  In Clink versions before v1.2.11 the
/// <code>clink.print()</code> API exists (undocumented) but accepts exactly one
/// string argument and is therefore not fully compatible with normal
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-print">print()</a>
/// syntax.  If you use fewer or more than 1 argument or if the argument is not
/// a string, then first checking the Clink version (e.g.
/// <a href="#clink.version_encoded">clink.version_encoded</a>) can avoid
/// runtime errors.
/// -show:  clink.print("\x1b[32mgreen\x1b[m \x1b[35mmagenta\x1b[m")
/// -show:  -- Outputs "green" in green, a space, and "magenta" in magenta.
/// -show:
/// -show:  local a = "hello"
/// -show:  local world = 73
/// -show:  clink.print("a", a, "world", world)
/// -show:  -- Outputs "a       hello   world   73".
/// -show:
/// -show:  clink.print("hello", NONL)
/// -show:  clink.print("world")
/// -show:  -- Outputs "helloworld".
static int32 clink_print(lua_State* state)
{
    str<> out;
    bool nl = true;
    bool err = false;

    int32 n = lua_gettop(state);            // Number of arguments.
    lua_getglobal(state, "NONL");           // Special value `NONL`.
    lua_getglobal(state, "tostring");       // Function to convert to string (reused each loop iteration).

    int32 printed = 0;
    for (int32 i = 1; i <= n; i++)
    {
        // Check for magic `NONL` value.
        if (lua_compare(state, -2, i, LUA_OPEQ))
        {
            nl = false;
            continue;
        }

        // Call function to convert arg to a string.
        lua_pushvalue(state, -1);           // Function to be called (tostring).
        lua_pushvalue(state, i);            // Value to print.
        if (lua_state::pcall(state, 1, 1) != 0)
        {
            if (const char* error = lua_tostring(state, -1))
            {
                puts("");
                puts(error);
            }
            return 0;
        }

        // Get result from the tostring call.
        size_t l;
        const char* s = lua_tolstring(state, -1, &l);
        if (s == NULL)
        {
            err = true;
            break;                          // Allow accumulated output to be printed before erroring out.
        }
        lua_pop(state, 1);                  // Pop result.

        // Add tab character to the output.
        if (printed++)
            out << "\t";

        // Add string result to the output.
        out.concat(s, int32(l));
    }

    if (g_printer)
    {
        if (nl)
            out.concat("\n");
        g_printer->print(out.c_str(), out.length());
    }
    else
    {
        printf("%s%s", out.c_str(), nl ? "\n" : "");
    }

    if (err)
        return luaL_error(state, LUA_QL("tostring") " must return a string to " LUA_QL("print"));

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  CLINK_EXE
/// -ver:   1.0.0
/// -var:   string
/// The full path to the Clink executable file.

//------------------------------------------------------------------------------
/// -name:  clink.version_encoded
/// -ver:   1.1.10
/// -var:   integer
/// The Clink version number encoded as a single integer following the format
/// <span class="arg">Mmmmpppp</span> where <span class="arg">M</span> is the
/// major part, <span class="arg">m</span> is the minor part, and
/// <span class="arg">p</span> is the patch part of the version number.
///
/// For example, Clink v95.6.723 would be <code>950060723</code>.
///
/// This format makes it easy to test for feature availability by encoding
/// version numbers from the release notes.

//------------------------------------------------------------------------------
/// -name:  clink.version_major
/// -ver:   1.1.10
/// -var:   integer
/// The major part of the Clink version number.
/// For v<strong>1</strong>.2.3.a0f14d the major version is 1.

//------------------------------------------------------------------------------
/// -name:  clink.version_minor
/// -ver:   1.1.10
/// -var:   integer
/// The minor part of the Clink version number.
/// For v1.<strong>2</strong>.3.a0f14d the minor version is 2.

//------------------------------------------------------------------------------
/// -name:  clink.version_patch
/// -ver:   1.1.10
/// -var:   integer
/// The patch part of the Clink version number.
/// For v1.2.<strong>3</strong>.a0f14d the patch version is 3.

//------------------------------------------------------------------------------
/// -name:  clink.version_commit
/// -ver:   1.1.10
/// -var:   string
/// The commit part of the Clink version number.
/// For v1.2.3.<strong>a0f14d</strong> the commit part is a0f14d.



// BEGIN -- Clink 0.4.8 API compatibility --------------------------------------

extern "C" {
#include "lua.h"
#include <compat/config.h>
#include <readline/rlprivate.h>
}

extern int32 get_clink_setting(lua_State* state);
extern int32 glob_impl(lua_State* state, bool dirs_only, bool back_compat);
extern int32 lua_execute(lua_State* state);
extern int32 get_screen_info_impl(lua_State* state, bool back_compat);

//------------------------------------------------------------------------------
/// -name:  clink.get_console_aliases
/// -deprecated: os.getaliases

//------------------------------------------------------------------------------
/// -name:  clink.get_cwd
/// -deprecated: os.getcwd

//------------------------------------------------------------------------------
/// -name:  clink.get_env
/// -deprecated: os.getenv

//------------------------------------------------------------------------------
/// -name:  clink.get_env_var_names
/// -deprecated: os.getenvnames

//------------------------------------------------------------------------------
/// -name:  clink.find_dirs
/// -deprecated: os.globdirs
/// -arg:   mask:string
/// -arg:   [case_map:boolean]
/// The <span class="arg">case_map</span> argument is ignored, because match
/// generators are no longer responsible for filtering matches.  The match
/// pipeline itself handles that internally now.
int32 old_glob_dirs(lua_State* state)
{
    return glob_impl(state, true, true/*back_compat*/);
}

//------------------------------------------------------------------------------
/// -name:  clink.find_files
/// -deprecated: os.globfiles
/// -arg:   mask:string
/// -arg:   [case_map:boolean]
/// The <span class="arg">case_map</span> argument is ignored, because match
/// generators are no longer responsible for filtering matches.  The match
/// pipeline itself handles that internally now.
int32 old_glob_files(lua_State* state)
{
    return glob_impl(state, false, true/*back_compat*/);
}

//------------------------------------------------------------------------------
/// -name:  clink.get_setting_str
/// -deprecated: settings.get
static int32 get_setting_str(lua_State* state)
{
    return get_clink_setting(state);
}

//------------------------------------------------------------------------------
/// -name:  clink.get_setting_int
/// -deprecated: settings.get
static int32 get_setting_int(lua_State* state)
{
    return get_clink_setting(state);
}

//------------------------------------------------------------------------------
/// -name:  clink.is_dir
/// -deprecated: os.isdir

//------------------------------------------------------------------------------
/// -name:  clink.get_rl_variable
/// -deprecated: rl.getvariable
static int32 get_rl_variable(lua_State* state)
{
    // Check we've got at least one string argument.
    if (lua_gettop(state) == 0 || !lua_isstring(state, 1))
        return 0;

    const char* string = lua_tostring(state, 1);
    const char* rl_cvar = rl_variable_value(string);
    if (rl_cvar == nullptr)
        return 0;

    lua_pushstring(state, rl_cvar);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.is_rl_variable_true
/// -deprecated: rl.isvariabletrue
static int32 is_rl_variable_true(lua_State* state)
{
    int32 i;
    const char* cvar_value;

    i = get_rl_variable(state);
    if (i == 0)
    {
        return 0;
    }

    cvar_value = lua_tostring(state, -1);
    i = (_stricmp(cvar_value, "on") == 0) || (_stricmp(cvar_value, "1") == 0);
    lua_pop(state, 1);
    lua_pushboolean(state, i);

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.get_host_process
/// -deprecated:
/// Always returns "clink"; this corresponds to the "clink" word in the
/// <code>$if clink</code> directives in Readline's .inputrc file.
static int32 get_host_process(lua_State* state)
{
    lua_pushstring(state, rl_readline_name);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.get_screen_info
/// -deprecated: os.getscreeninfo
/// <strong>Note:</strong> The field names are different between
/// <code>os.getscreeninfo()</code> and the v0.4.9 implementation of
/// <code>clink.get_screen_info</code>.
static int32 get_screen_info(lua_State* state)
{
    return get_screen_info_impl(state, true/*back_compat*/);
}



// END -- Clink 0.4.8 API compatibility ----------------------------------------



//------------------------------------------------------------------------------
static int32 map_string(lua_State* state, transform_mode mode)
{
    const char* string;
    int32 length;

    // Check we've got at least one argument...
    if (lua_gettop(state) == 0)
        return 0;

    // ...and that the argument is a string.
    if (!lua_isstring(state, 1))
        return 0;

    string = lua_tostring(state, 1);
    length = (int32)strlen(string);

    str<> text;
    if (length)
        str_transform(string, length, text, mode);

    if (_rl_completion_case_map)
    {
        for (uint32 i = 0; i < text.length(); ++i)
        {
            if (text[i] == '-' && (mode != transform_mode::upper))
                text.data()[i] = '_';
            else if (text[i] == '_' && (mode == transform_mode::upper))
                text.data()[i] = '-';
        }
    }

    lua_pushlstring(state, text.c_str(), text.length());

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.lower
/// -ver:   0.4.9
/// -arg:   text:string
/// -ret:   string
/// This API correctly converts UTF8 strings to lowercase, with international
/// linguistic awareness.
/// -show:  clink.lower("Hello World") -- returns "hello world"
/// -show:  clink.lower("ÁÈÏõû")       -- returns "áèïõû"
static int32 to_lowercase(lua_State* state)
{
    return map_string(state, transform_mode::lower);
}

//------------------------------------------------------------------------------
/// -name:  clink.upper
/// -ver:   1.1.5
/// -arg:   text:string
/// -ret:   string
/// This API correctly converts UTF8 strings to uppercase, with international
/// linguistic awareness.
/// -show:  clink.upper("Hello World") -- returns "HELLO WORLD"
/// -show:  clink.lower("áèïÕÛ")       -- returns "ÁÈÏÕÛ"
static int32 to_uppercase(lua_State* state)
{
    return map_string(state, transform_mode::upper);
}

//------------------------------------------------------------------------------
static struct popup_del_callback_info
{
    lua_State*      m_state = nullptr;
    int32           m_ref = LUA_REFNIL;

    bool empty() const
    {
        return !m_state || m_ref == LUA_REFNIL;
    }

    bool init(lua_State* state, int32 idx)
    {
        assert(!m_state);
        m_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        if (m_ref != LUA_REFNIL)
            m_state = state;
        return !empty();
    }

    void clear()
    {
        if (m_state && m_ref != LUA_REFNIL)
            luaL_unref(m_state, LUA_REGISTRYINDEX, m_ref);
        m_state = nullptr;
        m_ref = LUA_REFNIL;
    }

    bool call(int32 index)
    {
        if (empty())
            return false;
        lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_ref);
        lua_pushinteger(m_state, index + 1);
        if (lua_state::pcall(m_state, 1, 1) != 0)
            return false;
        return lua_toboolean(m_state, -1);
    }
} s_del_callback_info;

//------------------------------------------------------------------------------
static bool popup_del_callback(int32 index)
{
    return s_del_callback_info.call(index);
}

//------------------------------------------------------------------------------
/// -name:  clink.popuplist
/// -ver:   1.2.17
/// -arg:   title:string
/// -arg:   items:table
/// -arg:   [index:integer]
/// -arg:   [del_callback:function]
/// -ret:   string, boolean, integer
/// Displays a popup list and returns the selected item.  May only be used
/// within a <a href="#luakeybindings">luafunc: key binding</a> or inside a
/// function registered with
/// <a href="#clink.onfiltermatches">clink.onfiltermatches()</a>.
///
/// <span class="arg">title</span> is required and captions the popup list.
///
/// <span class="arg">items</span> is a table of strings to display.
///
/// <span class="arg">index</span> optionally specifies the default item (or 1
/// if omitted).
///
/// <span class="arg">del_callback</span> optionally specifies a callback
/// function to be called when <kbd>Del</kbd> is pressed.  The function receives
/// the index of the selected item.  If the function returns true then the item
/// is deleted from the popup list.  This requires Clink v1.3.41 or higher.
///
/// The function returns one of the following:
/// <ul>
/// <li>nil if the popup is canceled or an error occurs.
/// <li>Three values:
/// <ul>
/// <li>string indicating the <code>value</code> field from the selected item
/// (or the <code>display</code> field if no value field is present).
/// <li>boolean which is true if the item was selected with <kbd>Shift</kbd> or
/// <kbd>Ctrl</kbd> pressed.
/// <li>integer indicating the index of the selected item in the original
/// <span class="arg">items</span> table.
/// </ul>
/// </ul>
///
/// Alternatively, the <span class="arg">items</span> argument can be a table of
/// tables with the following scheme:
/// -show:  {
/// -show:  &nbsp;   {
/// -show:  &nbsp;       value       = "...",   -- Required; this is returned if the item is chosen.
/// -show:  &nbsp;       display     = "...",   -- Optional; displayed instead of value.
/// -show:  &nbsp;       description = "...",   -- Optional; displayed in a dimmed color in a second column.
/// -show:  &nbsp;   },
/// -show:  &nbsp;   ...
/// -show:  }
///
/// The <code>value</code> field is returned if the item is chosen.
///
/// The optional <code>display</code> field is displayed in the popup list
/// instead of the <code>value</code> field.
///
/// The optional <code>description</code> field is displayed in a dimmed color
/// in a second column.  If it contains tab characters (<code>"\t"</code>) the
/// description string is split into multiple columns (up to 3).
///
/// Starting in v1.3.18, if any description contains a tab character, then the
/// descriptions are automatically aligned in a column.
///
/// Otherwise, the descriptions follow immediately after the display field.
/// They can be aligned in a column by making all of the display fields be the
/// same number of character cells.
///
/// Starting in v1.4.0, the <span class="arg">items</span> table may optionally
/// include any of the following fields to customize the popup list.  The color
/// strings must be
/// <a href="https://en.wikipedia.org/wiki/ANSI_escape_code#SGR">SGR parameters</a>
/// and will be automatically converted into the corresponding ANSI escape code.
/// -show:  {
/// -show:  &nbsp;   height          = 20,       -- Preferred height, not counting the border.
/// -show:  &nbsp;   width           = 60,       -- Preferred width, not counting the border.
/// -show:  &nbsp;   reverse         = true,     -- Start at bottom; search upwards.
/// -show:  &nbsp;   searchmode      = "filter", -- Use "find" or "filter" to override the default search mode (in v1.6.13 and higher).
/// -show:  &nbsp;   colors = {                  -- Override the popup colors using any colors in this table.
/// -show:  &nbsp;       items       = "97;44",  -- The items color (e.g. bright white on blue).
/// -show:  &nbsp;       desc        = "...",    -- The description color.
/// -show:  &nbsp;       border      = "...",    -- The border color (defaults to items color).
/// -show:  &nbsp;       header      = "...",    -- The title color (defaults to border).
/// -show:  &nbsp;       footer      = "...",    -- The footer message color (defaults to border color).
/// -show:  &nbsp;       select      = "...",    -- The selected item color (defaults to reverse video of items color).
/// -show:  &nbsp;       selectdesc  = "...",    -- The selected item description color (defaults to selected item color).
/// -show:  &nbsp;   }
/// -show:  }
static int32 popup_list(lua_State* state)
{
    if (!lua_state::is_in_luafunc() && !lua_state::is_in_onfiltermatches() && !lua_state::is_interpreter())
        return luaL_error(state, "use of " LUA_QL("clink.popuplist") " is restricted");

    enum arg_indices { makevaluesonebased, argTitle, argItems, argIndex, argDelCallback};

    const char* title = checkstring(state, argTitle);
    const bool has_index = !lua_isnoneornil(state, argIndex);
    const auto _index = optinteger(state, argIndex, 1);
    if (!title || !_index.isnum() || !lua_istable(state, argItems))
        return 0;
    int32 index = _index - 1;

    int32 num_items = int32(lua_rawlen(state, argItems));
    if (!num_items)
        return 0;

#ifdef DEBUG
    int32 top = lua_gettop(state);
#endif

    std::vector<autoptr<const char>> free_items;
    std::vector<const char*> items;
    free_items.reserve(num_items);
    items.reserve(num_items);
    for (int32 i = 1; i <= num_items; ++i)
    {
        lua_rawgeti(state, argItems, i);

        const char* value = nullptr;
        const char* display = nullptr;
        const char* description = nullptr;

        if (lua_istable(state, -1))
        {
            lua_pushliteral(state, "value");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                value = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "display");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                display = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "description");
            lua_rawget(state, -2);
            if (lua_isstring(state, -1))
                description = lua_tostring(state, -1);
            lua_pop(state, 1);
        }
        else
        {
            display = lua_tostring(state, -1);
        }

        if (!value && !display)
            value = display = "";
        else if (!display)
            display = value;
        else if (!value)
            value = display;

        size_t alloc_size = 3; // NUL terminators.
        alloc_size += strlen(value);
        alloc_size += strlen(display);
        if (description) alloc_size += strlen(description);

        str_moveable s;
        s.reserve(alloc_size);

        {
            char* p = s.data();
            append_string_into_buffer(p, value);
            append_string_into_buffer(p, display);
            append_string_into_buffer(p, description, true/*allow_tabs*/);
        }

        const char* p = s.detach();
        free_items.emplace_back(p);
        items.emplace_back(p);

        lua_pop(state, 1);
    }

#ifdef DEBUG
    assert(lua_gettop(state) == top);
    assert(num_items == items.size());
#endif

    if (index >= items.size()) index = items.size() - 1;
    if (index < 0) index = 0;

    popup_config config;

    if (lua_isfunction(state, argDelCallback))
    {
        lua_pushvalue(state, argDelCallback);
        if (s_del_callback_info.init(state, -1))
            config.del_callback = popup_del_callback;
    }

    lua_pushliteral(state, "height");
    lua_rawget(state, argItems);
    if (lua_isnumber(state, -1))
    {
        int32 n = lua_tointeger(state, -1);
        if (n > 0)
            config.height = n;
    }
    lua_pop(state, 1);

    lua_pushliteral(state, "width");
    lua_rawget(state, argItems);
    if (lua_isnumber(state, -1))
    {
        int32 n = lua_tointeger(state, -1);
        if (n > 0)
            config.width = n;
    }
    lua_pop(state, 1);

    lua_pushliteral(state, "reverse");
    lua_rawget(state, argItems);
    config.reverse = lua_toboolean(state, -1);
    if (config.reverse && !has_index)
        index = num_items - 1;
    lua_pop(state, 1);

    lua_pushliteral(state, "searchmode");
    lua_rawget(state, argItems);
    if (lua_isstring(state, -1))
    {
        const char* searchmode = lua_tostring(state, -1);
        if (searchmode)
        {
            if (stricmp(searchmode, "find") == 0)
                config.search_mode = 0;
            else if (stricmp(searchmode, "filter") == 0)
                config.search_mode = 1;
        }
    }
    lua_pop(state, 1);

    lua_pushliteral(state, "colors");
    lua_rawget(state, argItems);
    if (lua_istable(state, -1))
    {
        const char* s;

        lua_pushliteral(state, "items");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.items = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "desc");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.desc = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "border");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.border = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "header");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.header = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "footer");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.footer = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "select");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.select = s;
        lua_pop(state, 1);

        lua_pushliteral(state, "selectdesc");
        lua_rawget(state, -2);
        if (s = lua_tostring(state, -1))
            config.colors.selectdesc = s;
        lua_pop(state, 1);
    }
    lua_pop(state, 1);

    const popup_results results = activate_text_list(title, &*items.begin(), int32(items.size()), index, true/*has_columns*/, &config);

    s_del_callback_info.clear();

    switch (results.m_result)
    {
    case popup_result::select:
    case popup_result::use:
        lua_pushlstring(state, results.m_text.c_str(), results.m_text.length());
        lua_pushboolean(state, (results.m_result == popup_result::select));
        lua_pushinteger(state, results.m_index + 1);
        return 3;
    }

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.getpopuplistcolors
/// -ver:   1.4.0
/// -ret:   table
/// Returns the default popup colors in a table with the following scheme:
/// -show:  {
/// -show:  &nbsp;   items      = "...",   -- The SGR parameters for the items color.
/// -show:  &nbsp;   desc       = "...",   -- The SGR parameters for the description color.
/// -show:  &nbsp;   -- Clink v1.7.0 adds the following colors to the table:
/// -show:  &nbsp;   border     = "...",   -- The SGR parameters for the border color.
/// -show:  &nbsp;   header     = "...",   -- The SGR parameters for the title color.
/// -show:  &nbsp;   footer     = "...",   -- The SGR parameters for the footer message color.
/// -show:  &nbsp;   select     = "...",   -- The SGR parameters for the selected item color.
/// -show:  &nbsp;   selectdesc = "...",   -- The SGR parameters for the selected item description color.
/// -show:  }
/// See
/// <a href="https://en.wikipedia.org/wiki/ANSI_escape_code#SGR">SGR parameters</a>
/// for more information on ANSI escape codes for colors.
static int32 get_popup_list_colors(lua_State* state)
{
    struct table_t {
        const char* name;
        const char* value;
    };

    lua_createtable(state, 0, 7);
    {
        struct table_t table[] = {
            { "items", get_popup_colors() },
            { "desc", get_popup_desc_colors() },
            { "border", get_popup_border_colors() },
            { "header", get_popup_header_colors() },
            { "footer", get_popup_footer_colors() },
            { "select", get_popup_select_colors() },
            { "selectdesc", get_popup_selectdesc_colors() },
        };

        for (uint32 i = 0; i < sizeof_array(table); ++i)
        {
            const char* value = table[i].value;
            if (value[0] == '0' && value[1] == ';')
                value += 2;
            lua_pushstring(state, table[i].name);
            lua_pushstring(state, value);
            lua_rawset(state, -3);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.getsession
/// -ver:   1.1.44
/// -ret:   string
/// Returns the current Clink session id.
///
/// This is needed when using
/// <a href="https://www.lua.org/manual/5.2/manual.html#pdf-io.popen">io.popen()</a>
/// (or similar functions) to invoke <code>clink history</code> or <code>clink
/// info</code> while Clink is installed for autorun.  The popen API spawns a
/// new CMD.exe, which gets a new Clink instance injected, so the history or
/// info command will use the new session unless explicitly directed to use the
/// calling session.
/// -show:  local exe = string.format('"%s" --session %s', CLINK_EXE, clink.getsession())
/// -show:  local r = io.popen('2>nul '..exe..' history')
/// -show:  if r then
/// -show:  &nbsp;   for line in r:lines() do
/// -show:  &nbsp;       print(line)
/// -show:  &nbsp;   end
/// -show:  &nbsp;   r:close()
/// -show:  end
static int32 get_session(lua_State* state)
{
    str<32> session;
    session.format("%d", GetCurrentProcessId());
    lua_pushlstring(state, session.c_str(), session.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.getansihost
/// -ver:   1.1.48
/// -ret:   string
/// Returns up to two strings indicating who Clink thinks will currently handle
/// ANSI escape codes.
///
/// The first returned string is the "current" handler.  This can change based
/// on the <code><a href="#terminal_emulation">terminal.emulation</a></code>
/// setting.
///
/// Starting in v1.4.26 a second string can be returned which indicates the
/// "native" handler.  This is what Clink has detected as the terminal host and
/// is not affected by the `terminal.emulation` setting.
///
/// The returned strings will always be <code>"unknown"</code> until the first
/// edit prompt (see <a href="#clink.onbeginedit">clink.onbeginedit()</a>).
///
/// These can be useful in choosing what kind of ANSI escape codes to use, but
/// are a best guess and are not necessarily 100% reliable.
///
/// <table>
/// <tr><th>Return</th><th>Description</th></tr>
/// <tr><td>"unknown"</td><td>Clink doesn't know.</td></tr>
/// <tr><td>"clink"</td><td>Clink is emulating ANSI support.  256 color and 24 bit color escape
///     codes are mapped to the nearest of the 16 basic colors.</td></tr>
/// <tr><td>"conemu"</td><td>Clink thinks ANSI escape codes will be handled by ConEmu.</td></tr>
/// <tr><td>"ansicon"</td><td>Clink thinks ANSI escape codes will be handled by ANSICON.</td></tr>
/// <tr><td>"winterminal"</td><td>Clink thinks ANSI escape codes will be handled by Windows
///     Terminal.</td></tr>
/// <tr><td>"wezterm"</td><td>Clink thinks ANSI escape codes will be handled by WezTerm.</td></tr>
/// <tr><td>"winconsole"</td><td>Clink thinks ANSI escape codes will be handled by the default
///     console support in Windows, but Clink detected a terminal replacement that won't support 256
///     color or 24 bit color.</td></tr>
/// <tr><td>"winconsolev2"</td><td>Clink thinks ANSI escape codes will be handled by the default
///     console support in Windows, or it might be handled by a terminal replacement that Clink
///     wasn't able to detect.</td></tr>
/// </table>
static int32 get_ansi_host(lua_State* state)
{
    static const char* const s_handlers[] =
    {
        "unknown",
        "clink",
        "ansicon",
        "conemu",
        "winterminal",
        "wezterm",
        "winconsolev2",
        "winconsole",
    };

    static_assert(sizeof_array(s_handlers) == size_t(ansi_handler::max), "must match ansi_handler enum");

    size_t current_handler = size_t(get_current_ansi_handler());
    lua_pushstring(state, s_handlers[current_handler]);

    size_t native_handler = size_t(get_native_ansi_handler());
    lua_pushstring(state, s_handlers[native_handler]);

    return 2;
}

//------------------------------------------------------------------------------
/// -name:  clink.translateslashes
/// -ver:   1.2.7
/// -arg:   [mode:integer]
/// -ret:   integer
/// This overrides how Clink translates slashes in completion matches, which is
/// normally determined by the
/// <code><a href="#match_translate_slashes">match.translate_slashes</a></code>
/// setting.
///
/// This is reset every time match generation is invoked, so use a generator to
/// set this.
///
/// The <span class="arg">mode</span> specifies how to translate slashes when
/// generators add matches:
/// <table>
/// <tr><th>Mode</th><th>Description</th></tr>
/// <tr><td><code>0</code></td><td>No translation.</td></tr>
/// <tr><td><code>1</code></td><td>Translate using the system path separator (backslash on Windows).</td></tr>
/// <tr><td><code>2</code></td><td>Translate to slashes (<code>/</code>).</td></tr>
/// <tr><td><code>3</code></td><td>Translate to backslashes (<code>\</code>).</td></tr>
/// <tr><td><code>4</code></td><td>Translate using the first kind of slash found in the word being completed, or the system path separator if there are no slashes yet.  (Only available in Clink v1.6.16 and higher.)</td></tr>
/// </table>
///
/// If <span class="arg">mode</span> is omitted, then the function returns the
/// current slash translation mode without changing it.
///
/// <strong>Note:</strong>  Clink always generates file matches using the
/// system path separator (backslash on Windows), regardless what path
/// separator may have been typed as input.  Setting this to <code>0</code>
/// does not disable normalizing typed input paths when invoking completion;
/// it only disables translating slashes in custom generators.
/// -show:  -- This example affects all match generators, by using priority -1 to
/// -show:  -- run first and returning false to let generators continue.
/// -show:  -- To instead affect only one generator, call clink.translateslashes()
/// -show:  -- in its :generate() function and return true.
/// -show:  local force_slashes = clink.generator(-1)
/// -show:  function force_slashes:generate()
/// -show:  &nbsp;   clink.translateslashes(2)  -- Convert to slashes.
/// -show:  &nbsp;   return false               -- Allow generators to continue.
/// -show:  end
static int32 translate_slashes(lua_State* state)
{
    extern void set_slash_translation(int32 mode);
    extern int32 get_slash_translation();

    if (lua_isnoneornil(state, 1))
    {
        lua_pushinteger(state, get_slash_translation());
        return 1;
    }

    const auto _mode = checkinteger(state, 1);
    if (!_mode.isnum())
        return 0;
    int32 mode = _mode;

    if (mode < slash_translation::off || mode >= slash_translation::max)
        mode = slash_translation::system;

    set_slash_translation(mode);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.slash_translation
/// -deprecated: clink.translateslashes
/// -arg:   type:integer
/// Controls how Clink will translate the path separating slashes for the
/// current path being completed. Values for <span class="arg">type</span> are;
/// <ul>
/// <li>-1 - no translation</li>
/// <li>0 - to backslashes</li>
/// <li>1 - to forward slashes</li>
/// </ul>
static int32 api_slash_translation(lua_State* state)
{
    if (lua_gettop(state) == 0)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;

    int32 mode = int32(lua_tointeger(state, 1));
    if (mode < 0)           mode = slash_translation::off;
    else if (mode == 0)     mode = slash_translation::backslash;
    else if (mode == 1)     mode = slash_translation::slash;
    else                    mode = slash_translation::system;

    extern void set_slash_translation(int32 mode);
    set_slash_translation(mode);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.opensessionstream
/// -ver:   1.8.0
/// -arg:   name:string
/// -arg:   [mode:string]
/// -ret:   stream
/// Opens or creates a named in-memory stream that behaves like a Lua file
/// handle.  Unlike <code>io.open</code>, the stream is not stored in the file
/// system.  Instead it lives entirely in memory and exists for the duration
/// of the current Clink session.
///
/// The <span class="arg">name</span> string identifies the stream.  Any calls
/// with the same <span class="arg">name</span> string access the same stream.
///
/// The <span class="arg">mode</span> string can be any of the following:
/// <ul>
/// <li><code>"r"</code>: opens for read (the default), fails if the stream doesn't exist;
/// <li><code>"w"</code>: opens for write, all previous data is erased, creates the stream if it doesn't exist;
/// <li><code>"wx"</code>: opens for write, but fail if the stream already exists;
/// <li><code>"a"</code>: opens for writing at the end of the stream (append), writing is only allowed at the end of stream, creates the stream if it doesn't exist;
/// <li><code>"r+"</code>: opens for read and write, all previous data is preserved, fails if the stream doesn't exist;
/// <li><code>"w+"</code>: opens for read and write, all previous data is erased, creates the stream if it doesn't exist;
/// <li><code>"w+x"</code>: opens for read and write, all previous data is erased, but fail if the stream already exists;
/// <li><code>"a+"</code>: opens for reading and writing at the end of the stream (append), writing is only allowed at the end of stream, creates the stream if it doesn't exist.
/// </ul>
///
/// The <span class="arg">mode</span> string can also have a <code>'b'</code> at
/// the end to open the stream in binary mode.
///
/// Session streams are limited to 4 MB in size.
///
/// Scripts can use this function when data needs to survive across Lua VM
/// reboots, but not across different Clink sessions.  The `clink-reload`
/// command reboots the Lua VM inside the current Clink session, causing all
/// variables to be lost.  However, session streams created by
/// `clink.opensessionstream` are not lost.
/// -show:  -- Create or open a session stream named "history"
/// -show:  local s = clink.opensessionstream("my_history_stream", "w")
/// -show:  s:write("first command\n")
/// -show:  s:write("second command\n")
/// -show:  s:close()
/// -show:
/// -show:  -- Reopen the same stream later in the session
/// -show:  local r = clink.opensessionstream("my_history_stream", "r")
/// -show:  for line in r:lines() do
/// -show:  &nbsp;   print(line)
/// -show:  end
/// -show:  r:close()
static int32 open_session_stream(lua_State* state)
{
    const char* name = checkstring(state, 1);
    const char* mode = optstring(state, 2, "r");
    if (!name || !*name || !mode)
        return 0;

    luaL_SessionStream::OpenFlags flags = luaL_SessionStream::OpenFlags::NONE;
    const bool wr = (*mode == 'w');
    if (*mode == 'r')
        flags |= luaL_SessionStream::OpenFlags::READ;
    else if (*mode == 'w')
        flags |= luaL_SessionStream::OpenFlags::CREATE | luaL_SessionStream::OpenFlags::WRITE;
    else if (*mode == 'a')
        flags |= luaL_SessionStream::OpenFlags::CREATE | luaL_SessionStream::OpenFlags::WRITE | luaL_SessionStream::OpenFlags::APPEND;
    else
    {
bad_mode:
        luaL_argcheck(state, false, 2, "invalid mode");
    }
    ++mode;
    if (*mode == '+')
    {
        flags |= luaL_SessionStream::OpenFlags::READ | luaL_SessionStream::OpenFlags::WRITE;
        ++mode;
    }
    if (wr && *mode == 'x')
    {
        flags |= luaL_SessionStream::OpenFlags::ONLYCREATE;
        ++mode;
    }
    if (*mode == 'b')
    {
        flags |= luaL_SessionStream::OpenFlags::BINARY;
        ++mode;
    }
    if (*mode)
        goto bad_mode;
    auto* ss = luaL_SessionStream::make_new(state, name, flags, wr/*clear*/);
    return ss ? 1 : luaL_fileresult(state, 0, name);
}

//------------------------------------------------------------------------------
/// -name:  clink.reload
/// -ver:   1.2.29
/// Reloads Lua scripts and Readline config file at the next prompt.
static int32 reload(lua_State* state)
{
    force_reload_scripts();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.reclassifyline
/// -ver:   1.3.9
/// Reclassify the input line text again and refresh the input line display.
static int32 reclassify_line(lua_State* state)
{
    if (is_main_coroutine(state))
        reclassify(reclassify_reason::force);
    else
        lua_input_idle::signal_reclassify();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.refilterprompt
/// -ver:   1.2.46
/// Invoke the prompt filters again and refresh the prompt.
///
/// <strong>Note:</strong> This can potentially be expensive; call this only
/// infrequently.
extern bool g_filtering_in_progress;
int32 g_prompt_refilter = 0;
static int32 refilter_prompt(lua_State* state)
{
    if (g_filtering_in_progress)
        return luaL_error(state, LUA_QL("clink.refilterprompt") " may not be used within a prompt filter");

    // If called from a coroutine, schedule the refilter to happen when control
    // returns to the main coroutine.
    if (!is_main_coroutine(state))
    {
        save_stack_top ss(state);

        lua_getglobal(state, "clink");              // -4
        lua_pushliteral(state, "runonmain");        // -3
        lua_rawget(state, -2);                      // -3 (replaces -3)

        lua_getglobal(state, "clink");              // -2
        lua_pushliteral(state, "refilterprompt");   // -1
        lua_rawget(state, -2);                      // -1 (replaces -1)

        // The intent is to execute `clink.runonmain(clink.refilterprompt)`.
        // Calling lua_rawget only consumes one stack value, so the second
        // `clink` table must be removed to position `refilterprompt` as an
        // argument to `runonmain`.
        lua_remove(state, -2);

        if (lua_state::pcall_silent(state, 1, 0) != LUA_OK)
            assert("calling clink.runonmain(clink.refilterprompt) failed" == 0);
        return 0;
    }

    g_prompt_refilter++;
    void host_filter_prompt();
    host_filter_prompt();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.refilterafterterminalresize
/// -ver:   1.4.0
/// -arg:   refilter:boolean
/// -ret:   boolean
/// Call this with <span class="arg">refilter</span> either nil or true to make
/// Clink automatically rerun prompt filters after the terminal is resized.  The
/// previous value is returned.
///
/// On Windows the terminal is resized while the console program in the terminal
/// (such as CMD) continues to run.  If a console program writes to the terminal
/// while the resize is happening, then the terminal display can become garbled.
/// So Clink waits until the terminal has stayed the same size for at least 1.5
/// seconds, and then it reruns the prompt filters.
///
/// <strong>Use this with caution:</strong>  if the prompt filters have not been
/// designed efficiently, then rerunning them after resizing the terminal could
/// cause responsiveness problems.  Also, if the terminal is resized again while
/// the prompt filters are being rerun, then the terminal display may become
/// garbled.
static int32 refilter_after_terminal_resize(lua_State* state)
{
    bool refilter = true;
    if (!lua_isnoneornil(state, 1))
        refilter = lua_toboolean(state, 1);

    set_refilter_after_resize(refilter);
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  clink.parseline
/// -ver:   1.3.37
/// -arg:   line:string
/// -ret:   table
/// This parses the <span class="arg">line</span> string into a table of
/// commands, with one <a href="#line_state">line_state</a> for each command
/// parsed from the line string.
///
/// The returned table of tables has the following scheme:
/// -show:  local commands = clink.parseline("echo hello & echo world")
/// -show:  -- commands[1].line_state corresponds to "echo hello".
/// -show:  -- commands[2].line_state corresponds to "echo world".
static int32 parse_line(lua_State* state)
{
    const char* line = checkstring(state, 1);
    if (!line)
        return 0;

    // A word collector is lightweight, and there is currently no need to
    // support tokenisers for anything other than CMD.  So just create a
    // temporary one here.
    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;
    word_collector collector(&command_tokeniser, &word_tokeniser);

    // Collect words from the whole line.
    std::vector<word> tmp_words;
    std::vector<command> tmp_commands;
    uint32 len = uint32(strlen(line));
    const collect_words_mode tmp_mode = collect_words_mode::whole_command;
    collector.collect_words(line, len, 0, tmp_words, tmp_mode, &tmp_commands);

    // Group words into one line_state per command.
    command_line_states command_line_states;
    command_line_states.set(line, len, 0, tmp_words, tmp_mode, tmp_commands);

    // Make a deep copy in an object allocated in the Lua heap.  Garbage
    // collection will free it.
    line_states_lua::make_new(state, command_line_states.get_linestates(line, len));
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int32 g_prompt_redisplay = 0;
static int32 get_refilter_redisplay_count(lua_State* state)
{
    lua_pushinteger(state, g_prompt_refilter);
    lua_pushinteger(state, g_prompt_redisplay);
    return 2;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 is_transient_prompt_filter(lua_State* state)
{
    lua_pushboolean(state, prompt_filter::is_transient_filtering());
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 history_suggester(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const bool firstword = lua_toboolean(state, 2);
    const bool has_limit = !lua_isnoneornil(state, 3);
    int32 limit = has_limit ? checkinteger(state, 3).get() : -1;
    const int32 match_prev_cmd = lua_toboolean(state, 4);
    if (!line || (has_limit && limit <= 0))
        return 0;

    HIST_ENTRY** history = history_list();
    if (!history || history_length <= 0)
        return 0;

    // 'match_prev_cmd' only works when 'history.dupe_mode' is 'add'.
    if (match_prev_cmd && g_dupe_mode.get() != 0)
        return 0;

    // Make 'history' never contribute more than 10 suggestions.
    if (has_limit)
        limit = min(limit, 10);

    const char* prev_cmd = (match_prev_cmd && history_length > 0) ? history[history_length - 1]->line : nullptr;

    bool substr = false;
    int32 n = 0;
    lua_createtable(state, has_limit ? limit : 1, 0);

again:
    const DWORD tick = GetTickCount();
    const int32 scan_min = 100;
    const DWORD ms_max = substr ? 25 : 25;

    int32 scanned = 0;
    for (int32 i = history_length; --i >= 0;)
    {
        // Search at least SCAN_MIN entries.  But after that don't keep going
        // unless it's been less than MS_MAX milliseconds.
        if (scanned >= scan_min && !(scanned % 20) && GetTickCount() - tick >= ms_max)
            break;
        scanned++;

        int32 offset;
        int32 matchlen;
        if (substr)
        {
            offset = 0;
            matchlen = 0;
            for (const char* hline = history[i]->line; *hline; ++hline, ++offset)
            {
                str_iter lhs(line);
                str_iter rhs(hline);
                const int32 sublen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(lhs, rhs);
                if (sublen && !lhs.more() && (rhs.more() || sublen < 0))
                {
                    ++offset; // Convert from 0-based to 1-based.
                    matchlen = (sublen < 0) ? str_len(hline) : sublen;
                    break;
                }
            }
        }
        else
        {
            str_iter lhs(line);
            str_iter rhs(history[i]->line);
            matchlen = str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(lhs, rhs);
            offset = 1;

            // lhs isn't exhausted, or rhs is exhausted?  Continue searching.
            if (lhs.more() || !rhs.more())
                continue;
        }

        // Zero matching length?  Is ok with 'match_prev_cmd', otherwise
        // continue searching.
        if (!matchlen && !match_prev_cmd)
            continue;

        // Match previous command, if needed.
        if (match_prev_cmd)
        {
            if (i <= 0 || str_compare<char, false/*compute_lcd*/, true/*exact_slash*/>(prev_cmd, history[i - 1]->line) != -1)
                continue;
        }

        // Suggest this history entry.
        lua_createtable(state, 0, 2);

        lua_pushstring(state, history[i]->line);
        lua_rawseti(state, -2, 1);

        lua_pushinteger(state, 1);
        lua_rawseti(state, -2, 2);

        lua_pushliteral(state, "highlight");
        lua_createtable(state, 2, 0);
        {
            lua_pushinteger(state, offset);
            lua_rawseti(state, -2, 1);
            lua_pushinteger(state, matchlen);
            lua_rawseti(state, -2, 2);
        }
        lua_rawset(state, -3);

        lua_pushliteral(state, "history");
        lua_pushinteger(state, i + 1);
        lua_rawset(state, -3);

        lua_rawseti(state, -2, ++n);
        if (n >= limit)
            break;
    }

    if (n)
        return 1;

    // If collecting suggestions for the suggestion list and no prefix match
    // found in first pass, do a second pass looking for substring matches.
    if (has_limit && !match_prev_cmd && !substr)
    {
        substr = true;
        goto again;
    }

    lua_pop(state, 1);

    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 set_suggestion_started(lua_State* state)
{
    const char* line = checkstring(state, 1);
    if (!line)
        return 0;

    set_suggestion_started(line);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 set_suggestion_result(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const auto _endword_offset = checkinteger(state, 2);
    if (!line || !_endword_offset.isnum() || !lua_istable(state, 3))
        return 0;

    const int32 line_len = strlen(line);
    const int32 endword_offset = _endword_offset - 1;
    if (endword_offset < 0 || endword_offset > line_len)
        return 0;

    suggestions suggestions;

    if (lua_istable(state, 3))
    {
        lua_pushvalue(state, 3);

        const int32 len = int32(lua_rawlen(state, -1));
        for (int32 idx = 1; idx <= len; ++idx)
        {
            lua_rawgeti(state, -1, idx);

            if (lua_istable(state, -1))
            {
                lua_rawgeti(state, -1, 1);
                const char* suggestion = optstring(state, -1, nullptr);
                lua_pop(state, 1);

                lua_rawgeti(state, -1, 2);
                const auto _offset = optinteger(state, -1, 0);
                if (!_offset.isnum())
                    return 0;
                lua_pop(state, 1);

                int32 hs = -1;
                int32 hl = -1;
                lua_pushliteral(state, "highlight");
                lua_rawget(state, -2);
                if (lua_istable(state, -1))
                {
                    lua_rawgeti(state, -1, 1);
                    hs = optinteger(state, -1, 0) - 1;
                    lua_pop(state, 1);
                    lua_rawgeti(state, -1, 2);
                    hl = optinteger(state, -1, 0);
                    lua_pop(state, 1);
                    if (hs < 0 || hl < 0)
                        hs = hl = -1;
                }
                lua_pop(state, 1);

                lua_pushliteral(state, "tooltip");
                lua_rawget(state, -2);
                const char* tooltip = optstring(state, -1, nullptr);
                lua_pop(state, 1);

                lua_pushliteral(state, "source");
                lua_rawget(state, -2);
                const char* source = optstring(state, -1, nullptr);
                lua_pop(state, 1);

                int32 history = -1;
                if (source && !strcmp(source, "history"))
                {
                    lua_pushliteral(state, "history");
                    lua_rawget(state, -2);
                    const auto _history = optinteger(state, -1, -1);
                    lua_pop(state, 1);
                    history = _history - 1;
                    if (history < 0 || history >= history_length)
                        history = -1;
                }

                int32 offset = _offset - 1;
                if (offset < 0 || offset > line_len)
                    offset = line_len;
                if (!source || !*source)
                    source = "unknown";

                suggestions.add(suggestion, offset, source, hs, hl, tooltip, history);
            }

            lua_pop(state, 1);
        }

        lua_pop(state, 1);
    }

    set_suggestions(line, endword_offset, &suggestions);
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 is_suggestionlist_mode(lua_State* state)
{
    lua_pushboolean(state, is_suggestion_list_active(true/*even_if_hidden*/));
    return 1;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 kick_idle(lua_State* state)
{
    extern void kick_idle();
    kick_idle();
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 recognize_command(lua_State* state)
{
    const char* line = checkstring(state, 1);
    const char* word = checkstring(state, 2);
    const bool quoted = lua_toboolean(state, 3);
    if (!line || !word)
        return 0;
    if (!*line || !*word)
        return 0;

    bool ready;
    const recognition recognized = recognize_command(line, word, quoted, ready, nullptr/*file*/);
    lua_pushinteger(state, int32(recognized));
    return 1;
}

//------------------------------------------------------------------------------
static str_unordered_map<int32> s_cached_path_type;
static linear_allocator s_cached_path_store(2048);
static std::recursive_mutex s_cached_path_type_mutex;
void clear_path_type_cache()
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    s_cached_path_type.clear();
    s_cached_path_store.clear();
}
void add_cached_path_type(const char* full, int32 type)
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    dbg_ignore_scope(snapshot, "add_cached_path_type");
    uint32 size = uint32(strlen(full) + 1);
    char* key = static_cast<char*>(s_cached_path_store.alloc(size));
    memcpy(key, full, size);
    s_cached_path_type.emplace(key, type);
}
bool get_cached_path_type(const char* full, int32& type)
{
    std::lock_guard<std::recursive_mutex> lock(s_cached_path_type_mutex);
    const auto& iter = s_cached_path_type.find(full);
    if (iter == s_cached_path_type.end())
        return false;
    type = iter->second;
    return true;
}

//------------------------------------------------------------------------------
class path_type_async_lua_task : public async_lua_task
{
public:
    path_type_async_lua_task(const char* key, const char* src, const char* path)
    : async_lua_task(key, src)
    , m_path(path)
    {}

    int32 get_path_type() const { return m_type; }

protected:
    void do_work() override
    {
        m_type = os::get_path_type(m_path.c_str());
        add_cached_path_type(m_path.c_str(), m_type);
    }

private:
    str_moveable m_path;
    int32 m_type = os::path_type_invalid;
};

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
static int32 async_path_type(lua_State* state)
{
    const char* path = checkstring(state, 1);
    int32 timeout = optinteger(state, 2, 0);
    if (!path || !*path)
        return 0;

    str<280> full;
    os::get_full_path_name(path, full);

    int32 type;
    if (!get_cached_path_type(full.c_str(), type))
    {
        str_moveable key;
        key.format("async||%s", full.c_str());
        std::shared_ptr<async_lua_task> task = find_async_lua_task(key.c_str());
        bool created = !task;
        if (!task)
        {
            str<> src;
            get_lua_srcinfo(state, src);

            task = std::make_shared<path_type_async_lua_task>(key.c_str(), src.c_str(), full.c_str());
            if (task && lua_isfunction(state, 3))
            {
                dbg_ignore_scope(snapshot, "async path type");
                lua_pushvalue(state, 3);
                int32 ref = luaL_ref(state, LUA_REGISTRYINDEX);
                task->set_callback(std::make_shared<callback_ref>(ref));
            }

            add_async_lua_task(task);
        }

        if (timeout)
            WaitForSingleObject(task->get_wait_handle(), timeout);

        if (!task->is_complete())
            return 0;

        std::shared_ptr<path_type_async_lua_task> pt_task = std::dynamic_pointer_cast<path_type_async_lua_task>(task);
        if (!pt_task)
            return 0;

        pt_task->disable_callback();

        type = pt_task->get_path_type();
    }

    const char* ret = nullptr;
    switch (type)
    {
    case os::path_type_file:    ret = "file"; break;
    case os::path_type_dir:     ret = "dir"; break;
    default:                    return 0;
    }

    lua_pushstring(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  clink.recognizecommand
/// -ver:   1.3.38
/// -arg:   [line:string]
/// -arg:   word:string
/// -arg:   [quoted:boolean]
/// -ret:   word_class:string, ready:boolean, file:string
/// This reports the input line coloring word classification to use for a
/// command word.  The return value can be passed into
/// <a href="#word_classifications:classifyword">word_classifications:classifyword()</a>
/// as its <span class="arg">word_class</span> argument.
///
/// This is intended for advanced input line coloring purposes.  For example if
/// a script uses <a href="#clink.onfilterinput">clink.onfilterinput()</a> to
/// modify the input text, then it can use this function inside a custom
/// <a href="#classifier_override_line">classifier</a> to look up the color
/// appropriate for the modified input text.
///
/// The <span class="arg">line</span> is optional and may be an empty string or
/// omitted.  When present, it is parsed to check if it would be processed as a
/// <a href="#directory-shortcuts">directory shortcut</a>.
///
/// The <span class="arg">word</span> is a string indicating the word to be
/// analyzed.
///
/// The <span class="arg">quoted</span> is optional.  When true, it indicates
/// the word is quoted and any <code>^</code> characters are taken as-is,
/// rather than treating them as the usual CMD escape character.
///
/// The possible return values for <span class="arg">word_class</span> are:
///
/// <table>
/// <tr><th>Code</th><th>Classification</th><th>Clink Color Setting</th></tr>
/// <tr><td><code>"x"</code></td><td>Executable; used for the first word when it is not a command or doskey alias, but is an executable name that exists.</td><td><code><a href="#color_executable">color.executable</a></code></td></tr>
/// <tr><td><code>"u"</code></td><td>Unrecognized; used for the first word when it is not a command, doskey alias, or recognized executable name.</td><td><code><a href="#color_unrecognized">color.unrecognized</a></code></td></tr>
/// <tr><td><code>"o"</code></td><td>Other; used for file names and words that don't fit any of the other classifications.</td><td><code><a href="#color_input">color.input</a></code></td></tr>
/// </table>
///
/// The possible return values for <span class="arg">ready</span> are:
///
/// <ul>
/// <li>True if the analysis has completed.</li>
/// <li>False if the analysis has not yet completed (and the returned word class
/// may be a temporary placeholder).</li>
/// </ul>
///
/// The return value for <span class="arg">file</span> is the fully qualified
/// path to the found executable file, if any, or nil.
///
/// <strong>Note:</strong>  This always returns immediately, and it uses a
/// background thread to analyze the <span class="arg">word</span> asynchronously.
/// When the background thread finishes analyzing the word, Clink automatically
/// redisplays the input line, giving classifiers a chance to call this function
/// again and get the final <span class="arg">word_class</span> result.
static int32 api_recognize_command(lua_State* state)
{
    int32 iword = lua_isstring(state, 2) ? 2 : 1;
    int32 iline = iword - 1;
    const char* line = (iline < 1 || lua_isnil(state, iline)) ? "" : checkstring(state, iline);
    const char* word = checkstring(state, iword);
    const bool quoted = lua_toboolean(state, iword + 1);
    if (iline > 0 && (!line || !*line))
        return 0;
    if (!word || !*word)
        return 0;

    bool ready;
    str<> file;
    const recognition recognized = recognize_command(line, word, quoted, ready, &file);

    char cl = 'o';
    if (int32(recognized) < 0)
        cl = 'u';
    else if (int32(recognized) > 0)
        cl = 'x';
    lua_pushlstring(state, &cl, 1);
    lua_pushboolean(state, ready);
    if (file.empty())
        lua_pushnil(state);
    else
        lua_pushlstring(state, file.c_str(), file.length());
    return 3;
}

//------------------------------------------------------------------------------
static int32 generate_from_history(lua_State* state)
{
    LUA_ONLYONMAIN(state, "clink._generate_from_history");

    HIST_ENTRY** list = history_list();
    if (!list)
        return 0;

    cmd_command_tokeniser command_tokeniser;
    cmd_word_tokeniser word_tokeniser;
    word_collector collector(&command_tokeniser, &word_tokeniser);
    collector.init_alias_cache();

    save_stack_top ss(state);

    lua_getglobal(state, "clink");
    lua_pushliteral(state, "_generate_from_historyline");
    lua_rawget(state, -2);

    while (*list)
    {
        const char* buffer = (*list)->line;
        uint32 len = uint32(strlen(buffer));

        // Collect one line_state for each command in the line.
        std::vector<word> words;
        std::vector<command> commands;
        command_line_states command_line_states;
        const collect_words_mode mode = collect_words_mode::whole_command;
        collector.collect_words(buffer, len, len/*cursor*/, words, mode, &commands);
        command_line_states.set(buffer, len, 0, words, mode, commands);

        for (const line_state& line : command_line_states.get_linestates(buffer, len))
        {
            // clink._generate_from_historyline
            lua_pushvalue(state, -1);

            // line_state
            line_state_lua line_lua(line);
            line_lua.push(state);

            if (lua_state::pcall(state, 1, 0) != 0)
                break;
        }

        list++;
    }

    return 0;
}

//------------------------------------------------------------------------------
static int32 api_reset_generate_matches(lua_State* state)
{
    reset_generate_matches();
    return 0;
}

//------------------------------------------------------------------------------
static int32 mark_deprecated_argmatcher(lua_State* state)
{
    const char* name = checkstring(state, 1);
    if (name)
        mark_deprecated_argmatcher(name);
    return 0;
}

//------------------------------------------------------------------------------
static int32 signal_delayed_init(lua_State* state)
{
    lua_input_idle::signal_delayed_init();
    return 0;
}

//------------------------------------------------------------------------------
static int32 signal_reclassify_line(lua_State* state)
{
    reclassify(reclassify_reason::lazy_force);
    return 0;
}

//------------------------------------------------------------------------------
static int32 get_cmd_commands(lua_State* state)
{
    lua_createtable(state, 64, 0);

    const char* const* const lists[] =
    {
        c_cmd_exes,
        c_cmd_commands_basicwordbreaks,
        c_cmd_commands_shellwordbreaks,
    };

    uint32 i = 0;
    for (const auto list : lists)
    {
        for (const char* const* cmd = list; *cmd; ++cmd)
        {
            lua_pushinteger(state, ++i);
            lua_pushstring(state, *cmd);
            lua_rawset(state, -3);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
static int32 is_cmd_command(lua_State* state)
{
    const char* word = checkstring(state, 1);
    if (!word)
        return 0;

    lua_pushboolean(state, is_cmd_command(word));
    return 1;
}

//------------------------------------------------------------------------------
static int32 is_cmd_wordbreak(lua_State* state)
{
    line_state_lua* lsl = line_state_lua::check(state, 1);
    if (!lsl)
        return 0;

    const line_state* line_state = lsl->get_line_state();
    const uint32 cwi = line_state->get_command_word_index();
    const word& info = line_state->get_words()[cwi];
    const char* line = line_state->get_line();

    str<> word;
    word.concat(line + info.offset, info.length);

    const state_flag flag = is_cmd_command(word.c_str());
    bool cmd_wordbreak = !!(flag & state_flag::flag_specialwordbreaks);

    if (!cmd_wordbreak)
    {
        bool ready;
        str<> file;
        const recognition recognized = recognize_command(nullptr, word.c_str(), info.quoted, ready, &file);
        if (ready)
        {
            const char* ext = path::get_extension(file.c_str());
            if (ext)
            {
                cmd_wordbreak = (_strcmpi(ext, ".bat") == 0 ||
                                 _strcmpi(ext, ".cmd") == 0);
            }
        }
    }

    lua_pushboolean(state, cmd_wordbreak);
    return 1;
}

//------------------------------------------------------------------------------
static int32 find_match_highlight(lua_State* state)
{
    const char* match = checkstring(state, 1);
    const char* _typed = checkstring(state, 2);
    if (!match || !_typed)
        return 0;
    if (!*match || !*_typed)
        return 0;

    str<> tmp;
    const char* typed = _typed;

again:
    int32 best_offset = -1;
    int32 best_length = -1;
    for (const char* walk = match; *walk; ++walk)
    {
        const int32 result = str_compare(walk, typed);
        if (result < 0)
        {
            best_offset = uint32(walk - match);
            best_length = str_len(typed);
            break;
        }
        else if (result > best_length)
        {
            best_offset = uint32(walk - match);
            best_length = result;
        }
    }

    if (best_offset < 0 || best_length <= 0)
    {
        // If no match found and match.wild is enabled and typed has * or ?
        // then assume they are wildcards:  find the first non-wildcard
        // segment in the typed string and try searching for that.
        //
        // Why a post-processing approximation of the actual filename matching
        // that the match pipeline performs?  Because it's not worth the cost
        // of implementing something more pedantically precise.  And anyway,
        // technically wildcard matches always actually match the _entire_
        // string, so running a wildcard comparison would simply highlight the
        // entire match.  This is simpler and faster and yields more desirable
        // results except maybe in obscure edge cases.
        if (g_match_wild.get() && !tmp.length() && strpbrk(typed, "*?"))
        {
            const char* walk = _typed;
            while (*walk == '*' || *walk == '?')
                ++walk;
            while (*walk && *walk != '*' && *walk != '?')
            {
                tmp.concat(walk, 1);
                ++walk;
            }
            if (tmp.length())
            {
                typed = tmp.c_str();
                goto again;
            }
        }
        return 0;
    }

    lua_pushinteger(state, best_offset);
    lua_pushinteger(state, best_length);
    return 2;
}

//------------------------------------------------------------------------------
static int32 save_global_modes(lua_State* state)
{
    bool new_coroutine = lua_toboolean(state, 1);
    uint32 modes = lua_state::save_global_states(new_coroutine);
    lua_pushinteger(state, modes);
    return 1;
}

//------------------------------------------------------------------------------
static int32 restore_global_modes(lua_State* state)
{
#ifdef DEBUG
    const auto modes = checkinteger(state, 1);
    if (!modes.isnum())
        return 0;
#else
    uint32 modes = optinteger(state, 1, 0);
#endif
    lua_state::restore_global_states(modes);
    return 0;
}

//------------------------------------------------------------------------------
static int32 get_installation_type(lua_State* state)
{
    // Open the Uninstall key.

    HKEY hkey;
    wstr<> where(c_uninstall_key);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey))
    {
failed:
        lua_pushliteral(state, "zip");
        return 1;
    }

    // Get binaries path.

    WCHAR long_bin_dir[MAX_PATH * 2];
    {
        str<> tmp;
        if (!os::get_env("=clink.bin", tmp))
            goto failed;

        wstr<> bin_dir(tmp.c_str());
        DWORD len = GetLongPathNameW(bin_dir.c_str(), long_bin_dir, sizeof_array(long_bin_dir));
        if (!len || len >= sizeof_array(long_bin_dir))
            goto failed;

        long_bin_dir[len] = '\0';
    }

    // Enumerate installed programs.

    bool found = false;
    WCHAR install_key[MAX_PATH];
    install_key[0] = '\0';

    for (DWORD index = 0; true; ++index)
    {
        DWORD size = sizeof_array(install_key); // Characters, not bytes, for RegEnumKeyExW.
        if (ERROR_NO_MORE_ITEMS == RegEnumKeyExW(hkey, index, install_key, &size, 0, nullptr, nullptr, nullptr))
            break;

        if (size >= sizeof_array(install_key))
            size = sizeof_array(install_key) - 1;
        install_key[size] = '\0';

        // Ignore if not a Clink installation.
        if (_wcsnicmp(install_key, L"clink_", 6))
            continue;

        HKEY hsubkey;
        if (RegOpenKeyExW(hkey, install_key, 0, MAXIMUM_ALLOWED, &hsubkey))
            continue;

        DWORD type;
        WCHAR location[280];
        DWORD len = sizeof(location); // Bytes, not characters, for RegQueryValueExW.
        LSTATUS status = RegQueryValueExW(hsubkey, L"InstallLocation", NULL, &type, LPBYTE(&location), &len);
        RegCloseKey(hsubkey);

        if (status)
            continue;

        len = len / 2;
        if (len >= sizeof_array(location))
            continue;
        location[len] = '\0';

        // If the uninstall location matches the current binaries directory,
        // then this is a match.
        WCHAR long_location[MAX_PATH * 2];
        len = GetLongPathNameW(location, long_location, sizeof_array(long_location));
        if (len && len < sizeof_array(long_location) && !_wcsicmp(long_bin_dir, long_location))
        {
            found = true;
            break;
        }
    }

    RegCloseKey(hkey);

    if (!found)
        goto failed;

    str<> tmp(install_key);
    lua_pushliteral(state, "exe");
    lua_pushstring(state, tmp.c_str());
    return 2;
}

//------------------------------------------------------------------------------
static int32 set_install_version(lua_State* state)
{
    const char* key = checkstring(state, 1);
    const char* ver = checkstring(state, 2);
    if (!key || !ver || _strnicmp(key, "clink_", 6))
        return 0;

    if (ver[0] == 'v')
        ver++;

    wstr<> where(c_uninstall_key);
    wstr<> wkey(key);
    where << L"\\" << wkey.c_str();

    HKEY hkey;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, where.c_str(), 0, MAXIMUM_ALLOWED, &hkey);
    if (status)
        return 0;

    wstr<> name;
    wstr<> version(ver);
    name << L"Clink v" << version.c_str();

    bool ok = true;
    ok = ok && !RegSetValueExW(hkey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()), (name.length() + 1) * sizeof(*name.c_str()));
    ok = ok && !RegSetValueExW(hkey, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()), (version.length() + 1) * sizeof(*version.c_str()));
    RegCloseKey(hkey);

    if (!ok)
        return 0;

    lua_pushboolean(state, true);
    return 1;
}

//------------------------------------------------------------------------------
static int32 expand_prompt_codes(lua_State* state)
{
    const char* in = checkstring(state, 1);
    const bool rprompt = lua_toboolean(state, 2);
    if (!in)
        return 0;

    str<> out;
    const expand_prompt_flags flags = rprompt ? expand_prompt_flags::single_line : expand_prompt_flags::none;
    prompt_utils::expand_prompt_codes(in, out, flags);

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
static int32 is_skip_update(lua_State* state)
{
    const char* new_ver = checkstring(state, 1);
    if (!new_ver)
        return 0;

    str<16> reg_ver;
    if (!is_update_skipped(new_ver, &reg_ver))
        return 0;

    lua_pushboolean(state, true);
    lua_pushlstring(state, reg_ver.c_str(), reg_ver.length());
    return 2;
}

//------------------------------------------------------------------------------
static int32 is_snoozed_update(lua_State* state)
{
    lua_pushboolean(state, is_update_prompt_snoozed());
    return 1;
}

//------------------------------------------------------------------------------
static int32 reset_update_keys(lua_State* state)
{
    wstr<> keyname;
    wstr<> valname;

    static const char* const c_subkeys[] =
    {
        "SnoozeUpdatePrompt",
        "SkipUpdate",
    };

    for (const auto subkey : c_subkeys)
    {
        if (make_key_value_names(subkey, keyname, valname))
        {
            HKEY hkey;
            LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, keyname.c_str(), 0, KEY_CREATE_SUB_KEY|KEY_SET_VALUE, &hkey);
            if (status == ERROR_SUCCESS)
            {
                status = RegDeleteValueW(hkey, valname.c_str());
                RegCloseKey(hkey);
            }
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
static bool view_releases_page(HWND hdlg, uint32 index)
{
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecute(hdlg, nullptr, "https://github.com/chrisant996/clink/releases", 0, 0, SW_NORMAL);
    return false;
}

//------------------------------------------------------------------------------
static int32 show_update_prompt(lua_State* state)
{
    const char* ver = checkstring(state, 1);
    if (!ver || !*ver)
        return 0;

#ifdef DEBUG
    if (!lua_toboolean(state, 2))
    {
#endif

    assert(s_hMutex);
    if (!s_hMutex)
        return 0;

#ifdef DEBUG
    }
#endif

    if (is_update_prompt_snoozed() || is_update_skipped(ver))
    {
        lua_pushlstring(state, "ignore", 6);
        return 1;
    }

    str<> msg;
    str<> btn1;
    str<> btn2;
    msg.format("Clink %s is available.  What would you like to do?", ver);
    btn1.format("Install the %s update now.", ver);
    btn2.format("Skip the %s update.", ver);

    enum { btn_cancel, btn_install, btn_skip, btn_later, btn_notes };

    command_link_dialog dlg;
    dlg.add(btn_install, "&Update now", btn1.c_str());
    dlg.add(btn_skip, "&Skip this update", btn2.c_str());
    dlg.add(btn_later, "Wait until &later", "Do nothing now, but ask again later.");
    dlg.add(btn_notes, "View &Releases Page", "https://github.com/chrisant996/clink/releases", view_releases_page);

    const char* ret = "cancel";
    switch (dlg.do_modal(0, 180, "Clink Update", msg.c_str()))
    {
    case btn_install:
        ret = "update";
        break;
    case btn_skip:
        ret = "skip";
        skip_update(ver);
        break;
    case btn_later:
        ret = "later";
        snooze_update_prompt();
        break;
    }

    lua_pushstring(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
static int32 acquire_updater_mutex(lua_State* state)
{
    lua_pushboolean(state, acquire_updater_mutex());
    return 1;
}

//------------------------------------------------------------------------------
static int32 release_updater_mutex(lua_State* state)
{
    release_updater_mutex();
    return 0;
}

//------------------------------------------------------------------------------
static int32 get_scripts_path(lua_State* state)
{
    int32 id;
    host_context context;
    host_get_app_context(id, context);

    lua_pushstring(state, context.scripts.c_str());
    return 1;
}

//------------------------------------------------------------------------------
static int32 is_break_on_error(lua_State* state)
{
    extern bool g_force_break_on_error;
    lua_pushboolean(state, g_force_break_on_error || g_lua_breakonerror.get());
    return 1;
}

//------------------------------------------------------------------------------
#if defined(DEBUG) && defined(_MSC_VER)
#if defined(USE_MEMORY_TRACKING)
static int32 last_allocation_number(lua_State* state)
{
    lua_pushinteger(state, dbggetallocnumber());
    return 1;
}
#endif
#endif

//------------------------------------------------------------------------------
#if defined(DEBUG) && defined(_MSC_VER)
static int32 get_c_callstack(lua_State* state)
{
    str_moveable stk;
    int32 skip_frames = optnumber(state, 1, 0);
    bool new_lines = lua_isnoneornil(state, 2) ? true : lua_toboolean(state, 2);
    if (stk.reserve(16384))
        format_callstack(skip_frames, 99, stk.data(), stk.size(), new_lines);
    lua_pushlstring(state, stk.c_str(), stk.length());
    return 1;
}
#endif

//------------------------------------------------------------------------------
// This prototype was abandoned, but is kept in case parts of it might be useful
// in the future.  The match generator layer is too dependent on CMD and threads
// and things that don't make sense in a standalone Lua interpreter.  And how to
// initialize textlist_impl or Readline or various other things differs based on
// whether they're needed for normal usage or for the lua_editor_tester.
// There's very little value in this niche mode, and the cost and compatibility
// conflicts make it not worth pursing further.
#ifdef CLINK_USE_LUA_EDITOR_TESTER
static int32 run_editor_test(lua_State* state)
{
    // Arg 1 is input line text.
    const char* input = checkstring(state, 1);
    if (!input)
        return 0;

    // Arg 2 is table of expectations.
    if (!lua_istable(state, 2))
    {
        const char* expected = lua_typename(state, LUA_TTABLE);
        const char* got = luaL_typename(state, 2);
        const char* msg = lua_pushfstring(state, "%s expected, got %s", expected, got);
        return luaL_argerror(state, 2, msg);
    }

    rollback<printer*> rb_printer(g_printer, nullptr);
    os::cwd_restorer cwd;

    str_moveable message;
    lua_editor_tester tester(state);
    tester.set_input(input);

    // Expected output.
    lua_getfield(state, 2, "output");
    tester.set_expected_output(lua_tostring(state, -1));
    lua_pop(state, 1);

    // Expected matches.
    lua_getfield(state, 2, "matches");
    if (lua_istable(state, -1))
    {
        std::vector<str_moveable> matches;
        const int32 len = int32(lua_rawlen(state, -1));
        for (int32 idx = 1; idx <= len; ++idx)
        {
            lua_rawgeti(state, -1, idx);
            if (const char* s = lua_tostring(state, -1))
                matches.emplace_back(s);
            lua_pop(state, 1);
        }
        tester.set_expected_matches(matches);
    }
    lua_pop(state, 1);

    // Expected classifications.
    lua_getfield(state, 2, "classifications");
    tester.set_expected_classifications(lua_tostring(state, -1));
    lua_pop(state, 1);

    // Run test.
    const bool ok = tester.run(message);

    lua_pushboolean(state, ok);
    if (!ok)
    {
        lua_pushlstring(state, message.c_str(), message.length());
        return 2;
    }

    return 1;
}
#endif // CLINK_USE_LUA_EDITOR_TESTER



//------------------------------------------------------------------------------
extern int32 set_current_dir(lua_State* state);
extern int32 get_aliases(lua_State* state);
extern int32 get_current_dir(lua_State* state);
extern int32 get_env(lua_State* state);
extern int32 get_env_names(lua_State* state);
extern int32 is_dir(lua_State* state);
extern int32 explode(lua_State* state);

//------------------------------------------------------------------------------
void clink_lua_initialise(lua_state& lua, bool lua_interpreter)
{
    struct method_def {
        bool        always;
        const char* name;
        int32       (*method)(lua_State*);
    };

    static const method_def methods[] = {
        // APIs in the "clink." namespace.
        { 1,    "lower",                  &to_lowercase },
        { 1,    "print",                  &clink_print },
        { 1,    "upper",                  &to_uppercase },
        { 1,    "popuplist",              &popup_list },
        { 1,    "getpopuplistcolors",     &get_popup_list_colors },
        { 0,    "getsession",             &get_session },
        { 1,    "getansihost",            &get_ansi_host },
        { 0,    "translateslashes",       &translate_slashes },
        { 0,    "reload",                 &reload },
        { 0,    "reclassifyline",         &reclassify_line },
        { 0,    "refilterprompt",         &refilter_prompt },
        { 0,    "refilterafterterminalresize", &refilter_after_terminal_resize },
        { 1,    "parseline",              &parse_line },
        { 0,    "recognizecommand",       &api_recognize_command },
        { 1,    "opensessionstream",      &open_session_stream },
        // Backward compatibility with the Clink 0.4.8 API.  Clink 1.0.0a1 had
        // moved these APIs away from "clink.", but backward compatibility
        // requires them here as well.
        { 1,    "chdir",                  &set_current_dir },
        { 1,    "execute",                &lua_execute },
        { 1,    "find_dirs",              &old_glob_dirs },
        { 1,    "find_files",             &old_glob_files },
        { 1,    "get_console_aliases",    &get_aliases },
        { 1,    "get_cwd",                &get_current_dir },
        { 1,    "get_env",                &get_env },
        { 1,    "get_env_var_names",      &get_env_names },
        { 0,    "get_host_process",       &get_host_process },
        { 0,    "get_rl_variable",        &get_rl_variable },
        { 1,    "get_screen_info",        &get_screen_info },
        { 0,    "get_setting_int",        &get_setting_int },
        { 0,    "get_setting_str",        &get_setting_str },
        { 1,    "is_dir",                 &is_dir },
        { 0,    "is_rl_variable_true",    &is_rl_variable_true },
        { 0,    "slash_translation",      &api_slash_translation },
        { 1,    "split",                  &explode },
        // UNDOCUMENTED; internal use only.
        { 0,    "istransientpromptfilter", &is_transient_prompt_filter },
        { 0,    "get_refilter_redisplay_count", &get_refilter_redisplay_count },
        { 0,    "history_suggester",      &history_suggester },
        { 0,    "set_suggestion_started", &set_suggestion_started },
        { 0,    "set_suggestion_result",  &set_suggestion_result },
        { 0,    "_is_suggestionlist_mode", &is_suggestionlist_mode },
        { 0,    "kick_idle",              &kick_idle },
        { 0,    "_recognize_command",     &recognize_command },
        { 0,    "_async_path_type",       &async_path_type },
        { 0,    "_generate_from_history", &generate_from_history },
        { 0,    "_reset_generate_matches", &api_reset_generate_matches },
        { 0,    "_mark_deprecated_argmatcher", &mark_deprecated_argmatcher },
        { 0,    "_signal_delayed_init",   &signal_delayed_init },
        { 0,    "_signal_reclassifyline", &signal_reclassify_line },
        { 0,    "_get_cmd_commands",      &get_cmd_commands },
        { 0,    "is_cmd_command",         &is_cmd_command },
        { 0,    "is_cmd_wordbreak",       &is_cmd_wordbreak },
        { 0,    "_find_match_highlight",  &find_match_highlight },
        { 0,    "_save_global_modes",     &save_global_modes },
        { 0,    "_restore_global_modes",  &restore_global_modes },
        { 0,    "_get_installation_type", &get_installation_type },
        { 0,    "_set_install_version",   &set_install_version },
        { 0,    "_expand_prompt_codes",   &expand_prompt_codes },
        { 0,    "_is_skip_update",        &is_skip_update },
        { 0,    "_is_snoozed_update",     &is_snoozed_update },
        { 0,    "_reset_update_keys",     &reset_update_keys },
        { 0,    "_show_update_prompt",    &show_update_prompt },
        { 0,    "_acquire_updater_mutex", &acquire_updater_mutex },
        { 0,    "_release_updater_mutex", &release_updater_mutex },
        { 0,    "_get_scripts_path",      &get_scripts_path },
        { 1,    "_is_break_on_error",     &is_break_on_error },
#if defined(DEBUG) && defined(_MSC_VER)
#if defined(USE_MEMORY_TRACKING)
        { 0,    "last_allocation_number", &last_allocation_number },
#endif
        { 0,    "get_c_callstack",        &get_c_callstack },
#endif
    };

#ifdef CLINK_USE_LUA_EDITOR_TESTER
    static const method_def standalone_methods[] = {
        { 1,    "runeditortest",          &run_editor_test },
    };
#endif

    clear_deprecated_argmatchers();

    lua_State* state = lua.get_state();

    lua_createtable(state, 0, sizeof_array(methods));

    for (const auto& method : methods)
    {
        if (method.always || !lua_interpreter)
        {
            lua_pushstring(state, method.name);
            lua_pushcfunction(state, method.method);
            lua_rawset(state, -3);
        }
    }

#ifdef CLINK_USE_LUA_EDITOR_TESTER
    if (lua_interpreter)
    {
        for (const auto& method : standalone_methods)
        {
            lua_pushstring(state, method.name);
            lua_pushcfunction(state, method.method);
            lua_rawset(state, -3);
        }
    }
#endif

    lua_pushliteral(state, "version_encoded");
    lua_pushinteger(state, CLINK_VERSION_ENCODED);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_major");
    lua_pushinteger(state, CLINK_VERSION_MAJOR);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_minor");
    lua_pushinteger(state, CLINK_VERSION_MINOR);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_patch");
    lua_pushinteger(state, CLINK_VERSION_PATCH);
    lua_rawset(state, -3);

    lua_pushliteral(state, "version_commit");
    lua_pushstring(state, AS_STR(CLINK_COMMIT));
    lua_rawset(state, -3);

    if (s_test_harness)
    {
        lua_pushliteral(state, "_is_test_harness");
        lua_pushboolean(state, true);
        lua_rawset(state, -3);
    }

#ifdef DEBUG
    lua_pushliteral(state, "DEBUG");
    lua_pushboolean(state, true);
    lua_rawset(state, -3);
#endif

    lua_setglobal(state, "clink");
}
