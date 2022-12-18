-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local suggesters = {}
local _cancel

if settings.get("lua.debug") or clink.DEBUG then
    clink.debug = clink.debug or {}
    clink.debug._suggesters = suggesters
end



--------------------------------------------------------------------------------
-- Returns true when canceled; otherwise nil.
local function _do_suggest(line, lines, matches) -- luacheck: no unused
    -- Reset cancel flag.
    _cancel = nil

    -- Protected call to suggesters.
    local impl = function(line, matches) -- luacheck: ignore 432
        local suggestion, offset
        local strategy = settings.get("autosuggest.strategy"):explode()
        for _, name in ipairs(strategy) do
            local suggester = suggesters[name]
            if suggester then
                local func = suggester.suggest
                if func then
                    suggestion, offset = func(suggester, line, matches)
                    if _cancel then
                        return
                    end
                    if suggestion ~= nil then
                        return suggestion, offset
                    end
                end
            end
            if _cancel then
                return
            end
        end
    end

    local ok, ret, ret2 = xpcall(impl, _error_handler_ret, line, matches)
    if not ok then
        print("")
        print("suggester failed:")
        print(ret)
        return
    end

    if _cancel then
        return true
    end

    local info = line:getwordinfo(line:getwordcount())
    clink.set_suggestion_result(line:getline(), info and info.offset or 1, ret, ret2)
end

--------------------------------------------------------------------------------
local function deferred_generate(line, lines, matches, builder, generation_id)
    -- Cancel the current _do_suggest.
    _cancel = true

    -- Start coroutine for match generation.
    clink._make_match_generate_coroutine(line, lines, matches, builder, generation_id)
end

--------------------------------------------------------------------------------
local function wrap(line, lines, matches, builder, generation_id)
    local w = { _line = line, _lines = lines, _matches = matches, _generation_id = generation_id }
    function w:ensure()
        if not self._ensured then
            self._ensured = true
            deferred_generate(line, lines, matches, builder, generation_id)
        end
    end

    for key, func in pairs(debug.getmetatable(matches).__index) do
        w[key] = function (...)
            w:ensure()
            return func(matches, table.unpack({...}, 2))
        end
    end

    return w
end

--------------------------------------------------------------------------------
function clink._suggest(line, lines, matches, builder, generation_id)
    if builder then
        matches = wrap(line, lines, matches, builder, generation_id)
    end

    return _do_suggest(line, lines, matches)
end

--------------------------------------------------------------------------------
function clink._list_suggesters()
    local list = {}
    for name,_ in pairs(suggesters) do
        table.insert(list, name)
    end
    return list
end

--------------------------------------------------------------------------------
function clink._print_suggesters()
    local list = {}
    for name,_ in pairs(suggesters) do
        table.insert(list, name)
    end
    table.sort(list, function(a, b) return a < b end)
    for _,name in ipairs(list) do
        print(name)
    end
end

--------------------------------------------------------------------------------
--- -name:  clink.suggester
--- -ver:   1.2.47
--- -arg:   name:string
--- -ret:   table
--- Creates and returns a new suggester object.  Suggesters are consulted in the
--- order their names are listed in the
--- <code><a href="#autosuggest.strategy">autosuggest.strategy</a></code>
--- setting.
---
--- Define on the object a <code>:suggest()</code> function that takes a
--- <a href="#line_state">line_state</a> argument which contains the input line,
--- and a <a href="#matches">matches</a> argument which contains the possible
--- completions.  The function can return nil to give the next suggester a
--- chance, or can return a suggestion (or an empty string) to stop looking for
--- suggestions.
---
--- In Clink v1.2.51 and higher, the function may return a suggestion and an
--- offset where the suggestion begins in the line.  This is useful if the
--- suggester wants to be able to insert the suggestion using the original
--- casing.  For example if you type "set varn" and a history entry is "set
--- VARNAME" then returning <code>"set VARNAME", 1</code> or
--- <code>"VARNAME", 5</code> can accept "set VARNAME" instead of "set varnAME".
---
--- See <a href="#customisingsuggestions">Customizing Suggestions</a> for more
--- information.
--- -show:  local doskeyarg = clink.suggester("doskeyarg")
--- -show:  function doskeyarg:suggest(line, matches)
--- -show:  &nbsp;   if line:getword(1) == "doskey" and
--- -show:  &nbsp;           line:getline():match("[ \t][^ \t/][^ \t]+=") and
--- -show:  &nbsp;           not line:getline():match("%$%*") then
--- -show:  &nbsp;       -- If the line looks like it defines a macro and doesn't yet add all
--- -show:  &nbsp;       -- arguments, suggest adding all arguments.
--- -show:  &nbsp;       if line:getline():sub(#line:getline()) == " " then
--- -show:  &nbsp;           return "$*"
--- -show:  &nbsp;       else
--- -show:  &nbsp;           return " $*"
--- -show:  &nbsp;       end
--- -show:  &nbsp;   end
--- -show:  end
function clink.suggester(name)
    if name == nil or name == "" then
        error("Suggesters must be named so they can be listed in the 'autosuggest.strategy' setting.")
    end
    if type(name) ~= "string" then
        error("Suggester name must be a string.")
    end
    if name:match(" ") then
        error("Suggester names cannot contain spaces.")
    end

    local ret = {}
    suggesters[name] = ret

    return ret
end

--------------------------------------------------------------------------------
function clink._diag_suggesters()
    if not settings.get("lua.debug") then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local norm = "\x1b[m"           -- Normal.

    local list = {}
    for n,s in pairs(suggesters) do
        table.insert(list, { name=n, suggest=(s and s.suggest) })
    end
    table.sort(list, function(a,b) return a.name < b.name end)

    local maxlen = 0
    for _,entry in ipairs(list) do
        local len = console.cellcount(entry.name)
        entry.len = len
        if len > maxlen then
            maxlen = len
        end
    end

    local any = false
    local fmt = "  %-"..maxlen.."s  :  %s"
    for _,entry in ipairs(list) do
        if entry.suggest then
            local info = debug.getinfo(entry.suggest, 'S')
            if not clink._is_internal_script(info.short_src) then
                if not any then
                    clink.print(bold.."suggesters:"..norm)
                    any = true
                end
                clink.print(string.format(fmt, entry.name, info.short_src..":"..info.linedefined))
            end
        end
    end
end



--------------------------------------------------------------------------------
local function suffix(line, suggestion, minlen) -- luacheck: no unused
    if suggestion then
        local info = line:getwordinfo(line:getwordcount())
        local endword = line:getline():sub(info.offset, line:getcursor())
        local matchlen = string.matchlen(suggestion, endword)
        if matchlen >= minlen and matchlen == #endword then
            return suggestion:sub(matchlen + 1);
        end
    end
end

--------------------------------------------------------------------------------
local history_suggester = clink.suggester("history")
function history_suggester:suggest(line, matches) -- luacheck: no unused
    return clink.history_suggester(line:getline(), false)
end

--------------------------------------------------------------------------------
local prevcmd_suggester = clink.suggester("match_prev_cmd")
function prevcmd_suggester:suggest(line, matches) -- luacheck: no unused
    return clink.history_suggester(line:getline(), true)
end

--------------------------------------------------------------------------------
local completion_suggester = clink.suggester("completion")
function completion_suggester:suggest(line, matches) -- luacheck: no unused
    local count = line:getwordcount()
    if count > 0 then
        local info = line:getwordinfo(count)
        if info.offset < line:getcursor() then
            local typed = line:getline():sub(info.offset, line:getcursor())
            for i = 1, 5, 1 do
                local m = matches:getmatch(i)
                if not m then
                    break
                elseif m ~= typed then
                    return m, info.offset
                end
            end
        end
    end
end
