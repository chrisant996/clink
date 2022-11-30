// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "match_builder_lua.h"
#include "lua_state.h"

#include <core/base.h>
#include <core/str.h>
#include <lib/matches.h>

//------------------------------------------------------------------------------
const char* const match_builder_lua::c_name = "match_builder_lua";
const match_builder_lua::method match_builder_lua::c_methods[] = {
    { "addmatch",           &add_match },
    { "addmatches",         &add_matches },
    { "isempty",            &is_empty },
    { "setappendcharacter", &set_append_character },
    { "setsuppressappend",  &set_suppress_append },
    { "setsuppressquoting", &set_suppress_quoting },
    { "setnosort",          &set_no_sort },
    { "setvolatile",        &set_volatile },
    // Only for backward compatibility:
    { "deprecated_addmatch", &deprecated_add_match },
    { "setmatchesarefiles", &set_matches_are_files },
    // UNDOCUMENTED; internal use only.
    { "clear_toolkit",      &clear_toolkit },
    { "matches_ready",      &matches_ready },
    {}
};



//------------------------------------------------------------------------------
match_builder_lua::match_builder_lua(match_builder& builder)
: m_builder(&builder)
{
}

//------------------------------------------------------------------------------
match_builder_lua::match_builder_lua(std::shared_ptr<match_builder_toolkit>& toolkit)
: m_builder(toolkit.get()->get_builder())
, m_toolkit(toolkit)
{
}

//------------------------------------------------------------------------------
match_builder_lua::~match_builder_lua()
{
}

//------------------------------------------------------------------------------
/// -name:  builder:addmatch
/// -ver:   1.0.0
/// -arg:   match:string|table
/// -arg:   [type:string]
/// -ret:   boolean
/// Adds a match.
///
/// The <span class="arg">match</span> argument is the match string to add.
///
/// The <span class="arg">type</span> argument is the optional match type, or
/// "none" if omitted (see below for the possible match types).
///
/// Alternatively, the <span class="arg">match</span> argument can be a table
/// with the following scheme:
/// -show:  {
/// -show:  &nbsp;   match           = "..."    -- [string] The match text.
/// -show:  &nbsp;   display         = "..."    -- [string] OPTIONAL; alternative text to display when listing possible completions.
/// -show:  &nbsp;   description     = "..."    -- [string] OPTIONAL; a description for the match.
/// -show:  &nbsp;   type            = "..."    -- [string] OPTIONAL; the match type.
/// -show:  &nbsp;   appendchar      = "..."    -- [string] OPTIONAL; character to append after the match.
/// -show:  &nbsp;   suppressappend  = t_or_f   -- [boolean] OPTIONAL; whether to suppress appending a character after the match.
/// -show:  }
///
/// <ul>
/// <li>The <code>display</code> field is optional, and is displayed instead of
/// the <code>match</code> field when listing possible completions.  It can even
/// include ANSI escape codes for colors, etc.  (Requires v1.2.38 or greater.)
/// <li>The <code>description</code> field is optional, and is displayed in
/// addition to <code>match</code> or <code>display</code> when listing possible
/// completions.  (Requires v1.2.38 or greater.)
/// <li>The <code>type</code> field is optional.  If omitted, then the
/// <span class="arg">type</span> argument is used for that element.
/// <li>The <code>appendchar</code> field is optional, and overrides the normal
/// behavior for only this match.  (Requires v1.3.1 or greater.)
/// <li>The <code>suppressappend</code> field is optional, and overrides the
/// normal behavior for only this match.  (Requires v1.3.1 or greater.)
/// </ul>
///
/// The match type affects how the match is inserted, displayed, and colored.
/// Some type modifiers may be combined with a match type.
///
/// <table>
/// <tr><th>Type</th><th>Description</th></tr>
/// <tr><td>"word"</td><td>Shows the whole word even if it contains slashes.</td></tr>
/// <tr><td>"arg"</td><td>Avoids appending a space if the match ends with a colon or equal sign.</td></tr>
/// <tr><td>"cmd"</td><td>Displays the match using <a href="#color_cmd">color.cmd</a>.</td></tr>
/// <tr><td>"alias"</td><td>Displays the match using <a href="#color_doskey">color.doskey</a>.</td></tr>
/// <tr><td>"file"</td><td>Shows only the last path component, with appropriate file coloring.</td></tr>
/// <tr><td>"dir"</td><td>Shows only the last path component and adds a trailing path separator, with appropriate directory coloring.</td></tr>
/// <tr><td>"none"</td><td>For backward compatibility the match is treated like "file", unless it ends with a path separator in which case it's treated like "dir".</td></tr>
/// </table>
///
/// <table>
/// <tr><th>Modifier</th><th>Description</th></tr>
/// <tr><td>"hidden"</td><td>This can be combined with "file" or "dir" to use <a href="#color_hidden">color.hidden</a> (e.g. "file,hidden").</td></tr>
/// <tr><td>"readonly"</td><td>This can be combined with "file" or "dir" to use <a href="#color_readonly">color.readonly</a> (e.g. "file,readonly").</td></tr>
/// <tr><td>"link"</td><td>This can be combined with "file" or "dir" to appropriate symlink coloring (e.g. "file,link").</td></tr>
/// <tr><td>"orphaned"</td><td>This can be combined with "link" to use appropriate orphaned symlink coloring (e.g. "file,link,orphaned").</td></tr>
/// </table>
///
/// See <a href="#completioncolors">Completion Colors</a> and
/// <a href="#colorsettings">Color Settings</a> for more information about
/// colors.
/// -show:  builder:addmatch("hello") -- type is "none"
/// -show:  builder:addmatch("some_word", "word")
/// -show:  builder:addmatch("/flag", "arg")
/// -show:  builder:addmatch("abbrev", "alias")
/// -show:  builder:addmatch({ match="foo.cpp", type="file" })
/// -show:  builder:addmatch({ match="bar", type="dir" })
/// -show:  builder:addmatch({ match=".git", type="dir,hidden" })
int match_builder_lua::add_match(lua_State* state)
{
    int ret = 0;
    if (lua_gettop(state) > 0)
    {
        const char* type_str = optstring(state, 2, "");
        if (!type_str)
            return 0;

        match_type type = to_match_type(type_str);
        ret = !!add_match_impl(state, 1, type);
    }

    lua_pushboolean(state, ret);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  builder:isempty
/// -ver:   1.3.9
/// -ret:   boolean
/// Returns whether the match builder is empty.  It is empty when no matches
/// have been added yet.
int match_builder_lua::is_empty(lua_State* state)
{
    lua_pushboolean(state, m_builder->is_empty());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  builder:setappendcharacter
/// -ver:   1.1.2
/// -arg:   [append:string]
/// Sets character to append after matches.  For example the <code>set</code>
/// match generator uses this to append "=" when completing matches, so that
/// completing <code>set USER</code> becomes <code>set USERDOMAIN=</code>
/// (rather than <code>set USERDOMAIN&nbsp;</code>).
int match_builder_lua::set_append_character(lua_State* state)
{
    const char* append = optstring(state, 1, "");
    if (!append)
        return 0;

    m_builder->set_append_character(*append);

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  builder:setsuppressappend
/// -ver:   1.1.2
/// -arg:   [state:boolean]
/// Sets whether to suppress appending anything after the match except a
/// possible closing quote.  For example the env var match generator uses this.
int match_builder_lua::set_suppress_append(lua_State* state)
{
    bool suppress = true;
    if (lua_gettop(state) > 0)
        suppress = (lua_toboolean(state, 1) != 0);

    m_builder->set_suppress_append(suppress);

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  builder:setsuppressquoting
/// -ver:   1.1.2
/// -arg:   [state:integer]
/// Sets whether to suppress quoting for the matches.  Set to 0 for normal
/// quoting, or 1 to suppress quoting, or 2 to suppress end quotes.  For example
/// the env var match generator sets this to 1 to overcome the quoting that
/// would normally happen for "%" characters in filenames.
int match_builder_lua::set_suppress_quoting(lua_State* state)
{
    int suppress = 1;
    if (lua_gettop(state) > 0)
    {
        int i = int(lua_tointeger(state, 1));
        if (i >= 0 && i <= 2)
            suppress = i;
    }

    m_builder->set_suppress_quoting(suppress);

    return 0;
}

//------------------------------------------------------------------------------
/// -name:  builder:setnosort
/// -ver:   1.3.3
/// Turns off sorting the matches.
int match_builder_lua::set_no_sort(lua_State* state)
{
    m_builder->set_no_sort();
    return 0;
}

//------------------------------------------------------------------------------
/// -name:  builder:setvolatile
/// -ver:   1.3.37
/// Forces the generated matches to be used only once.
///
/// Normally Clink tries to reuse the most recently generated list of matches,
/// if possible.  It is an optimization, to avoid doing potentally expensive
/// work multiple times in a row to generate the same list of matches when
/// nothing has changed.  Normally the optimization is beneficial, and typing
/// more letters in a word can simply filter the existing list of matches.
///
/// But sometimes an argument may have special syntax.  For example, an email
/// address argument might want to generate matches for names until the word
/// contains a <code>@</code>, and then it might want to generate matches for
/// domain names.  The optimization interferes with situations where parsing the
/// word produces a completely different list of possible matches.
///
/// Making the generated matches volatile ensures matches are generated anew
/// each time completion is invoked.
int match_builder_lua::set_volatile(lua_State* state)
{
    m_builder->set_volatile();
    return 0;
}

//------------------------------------------------------------------------------
// Undocumented because it exists only to enable the clink.add_match backward
// compatibility.
int match_builder_lua::deprecated_add_match(lua_State* state)
{
    m_builder->set_deprecated_mode();
    return add_match(state);
}

//------------------------------------------------------------------------------
// Undocumented because it exists only to enable the clink.matches_are_files
// backward compatibility.
int match_builder_lua::set_matches_are_files(lua_State* state)
{
    if (lua_gettop(state) <= 0 || lua_isnil(state, 1))
        m_builder->set_matches_are_files(true);
    else if (lua_isboolean(state, 1))
        m_builder->set_matches_are_files(lua_toboolean(state, 1) != 0);
    else
        m_builder->set_matches_are_files(int(lua_tointeger(state, 1)));
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int match_builder_lua::clear_toolkit(lua_State* state)
{
    if (m_toolkit)
        m_toolkit->clear();
    return 0;
}

//------------------------------------------------------------------------------
// UNDOCUMENTED; internal use only.
int match_builder_lua::matches_ready(lua_State* state)
{
    if (!m_toolkit)
        return 0;

    bool isnum;
    int id = checkinteger(state, 1, &isnum);
    if (!isnum)
        return 0;

    extern bool notify_matches_ready(std::shared_ptr<match_builder_toolkit> toolkit, int generation_id);
    lua_pushboolean(state, notify_matches_ready(m_toolkit, id));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  builder:addmatches
/// -ver:   1.0.0
/// -arg:   matches:table
/// -arg:   [type:string]
/// -ret:   integer, boolean
/// This is the equivalent of calling <a href="#builder:addmatch">builder:addmatch()</a>
/// in a for-loop. Returns the number of matches added and a boolean indicating
/// if all matches were added successfully.
///
/// The <span class="arg">matches</span> argument can be a table of match
/// strings, or a table of tables describing the matches.
///
/// The <span class="arg">type</span> argument is used as the type when a match
/// doesn't explicitly include a type, and is "none" if omitted.
/// -show:  builder:addmatches({"abc", "def"}) -- Adds two matches of type "none"
/// -show:  builder:addmatches({"abc", "def"}, "file") -- Adds two matches of type "file"
/// -show:  builder:addmatches({
/// -show:  &nbsp;   -- Same table scheme per entry here as in builder:addmatch()
/// -show:  &nbsp;   { match="remote/origin/master", type="word" },
/// -show:  &nbsp;   { match="remote/origin/topic", type="word" }
/// -show:  })
int match_builder_lua::add_matches(lua_State* state)
{
    if (lua_gettop(state) <= 0 || !lua_istable(state, 1))
    {
        lua_pushinteger(state, 0);
        lua_pushboolean(state, 0);
        return 2;
    }

    const char* type_str = optstring(state, 2, "");
    if (!type_str)
        return 0;

    match_type type = to_match_type(type_str);

    int count = 0;
    int total = int(lua_rawlen(state, 1));
    for (int i = 1; i <= total; ++i)
    {
        lua_rawgeti(state, 1, i);
        count += !!add_match_impl(state, -1, type);
        lua_pop(state, 1);
    }

    lua_pushinteger(state, count);
    lua_pushboolean(state, count == total);
    return 2;
}

//------------------------------------------------------------------------------
bool match_builder_lua::add_match_impl(lua_State* state, int stack_index, match_type type)
{
    if (lua_isstring(state, stack_index) || lua_isnumber(state, stack_index))
    {
        const char* match = lua_tostring(state, stack_index);
        return m_builder->add_match(match, type);
    }
    else if (lua_istable(state, stack_index))
    {
        const int orig_stack_index = stack_index;
        if (stack_index < 0)
            --stack_index;

        const char* match = nullptr;

        lua_pushliteral(state, "match");
        lua_rawget(state, stack_index);
        if (lua_isstring(state, -1))
            match = lua_tostring(state, -1);
        lua_pop(state, 1);

        if (match)
        {
            lua_pushliteral(state, "type");
            lua_rawget(state, stack_index);
            if (lua_isstring(state, -1))
                type = to_match_type(lua_tostring(state, -1));
            lua_pop(state, 1);

            match_desc desc(match, nullptr, nullptr, type);

            lua_pushliteral(state, "display");
            lua_rawget(state, stack_index);
            if (lua_isstring(state, -1))
                desc.display = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "description");
            lua_rawget(state, stack_index);
            if (lua_isstring(state, -1))
                desc.description = lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "appendchar");
            lua_rawget(state, stack_index);
            if (lua_isstring(state, -1))
                desc.append_char = *lua_tostring(state, -1);
            lua_pop(state, 1);

            lua_pushliteral(state, "suppressappend");
            lua_rawget(state, stack_index);
            if (lua_isboolean(state, -1))
                desc.suppress_append = lua_toboolean(state, -1);
            lua_pop(state, 1);

            // Undocumented; for internal use only.
            lua_pushliteral(state, "appenddisplay");
            lua_rawget(state, stack_index);
            if (lua_isboolean(state, -1))
                desc.append_display = lua_toboolean(state, -1);
            lua_pop(state, 1);

            // If the table defines a match, add the match.
            if (desc.match != nullptr)
                return m_builder->add_match(desc);
        }
        else
        {
            // The table is not a single match, but it might contain multiple
            // matches.  For backward compatibility with v0.4.9, recursively
            // enumerate elements in the table and add them.
            const int num = int(lua_rawlen(state, orig_stack_index));
            if (num > 0)
            {
                bool ret = false;
                for (int i = 1; i <= num; ++i)
                {
                    lua_rawgeti(state, orig_stack_index, i);
                    ret = add_match_impl(state, -1, type) || ret;
                    lua_pop(state, 1);
                }
                return ret;
            }
        }
    }

    return false;
}
