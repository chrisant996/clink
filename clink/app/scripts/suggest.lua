-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local suggesters = {}
local _cancel

if settings.get("lua.debug") or clink.DEBUG then
    -- Make it possible to inspect these locals in the debugger.
    clink.debug = clink.debug or {}
    clink.debug._suggesters = suggesters
end



--------------------------------------------------------------------------------
local function test_dupe(seen, entry, line, add)
    local es = entry[1] or entry.suggestion or nil
    local eo = entry[2] or entry.offset or nil
    local full = line:getline():sub(1, (eo or 0) - 1)..es
    if seen[full] then
        return true
    end
    if add then
        seen[full] = true
    end
end

--------------------------------------------------------------------------------
local function get_limit_history()
    local num = settings.get("suggestionlist.num_history")
    if num < 1 then
        return 1
    elseif num > 8 then
        return 8
    end
    return num
end

--------------------------------------------------------------------------------
local function aggregate(line, results, limit)
    local seen = {}
    local uniques = {}
    local begin_agg_index = 1
    local count = 0

    -- Remove duplicates for the first 3 history suggestions.  If any
    -- suggestions remain for other suggesters after that, then only keep the
    -- first 3 history suggestions.
    if results[1] and results[1][1] and results[1][1].source == "history" then
        begin_agg_index = 2
        local index_analyzed_history
        local limit_history = get_limit_history()
        uniques[1] = {}
        for j = 1, limit do
            local e = results[1][j]
            if not e then
                break
            end
            if not test_dupe(seen, e, line, true--[[add]]) then
                table.insert(uniques[1], e)
                index_analyzed_history = j
                count = count + 1
                if count >= limit_history then
                    break
                end
            end
        end
        uniques[1].keep = #uniques[1]
        local all_dupes = true
        for i = 2, limit do
            if not all_dupes then
                break
            end
            local t = results[i]
            if not t then
                break
            end
            for j = 1, limit do
                local e = t[j]
                if not e then
                    break
                end
                if not test_dupe(seen, e, line) then
                    all_dupes = false
                    break
                end
            end
        end
        if all_dupes then
            -- Add the rest of the history entries, removing duplicates.
            for j = index_analyzed_history + 1, limit do
                local e = results[1][j]
                if not e then
                    break
                end
                if not test_dupe(seen, e, line, true--[[add]]) then
                    table.insert(uniques[1], e)
                end
            end
            uniques[1].keep = #uniques[1]
            count = uniques[1].keep
        end
    end

    -- Collect the rest of the non-dupe entries.
    local total = count
    for i = begin_agg_index, limit do
        if not results[i] then
            break
        end
        local sub_results = { keep=0 }
        for j = 1, limit do
            local e = results[i][j]
            if not e then
                break
            end
            if not test_dupe(seen, e, line, true--[[add]]) then
                table.insert(sub_results, e)
            end
        end
        if sub_results[1] then
            sub_results.count = #sub_results
            total = total + sub_results.count
            table.insert(uniques, sub_results)
        end
    end

    -- Aggregate evenly from the suggestion sources.
    local kept = count
    local done = (kept == total)
    while not done do
        local any
        for i = begin_agg_index, limit do
            local t = uniques[i]
            if not t then
                break
            end
            if t.keep < t.count then
                any = true
                t.keep = t.keep + 1
                kept = kept + 1
                if kept >= limit then
                    done = true
                    break
                end
            end
        end
        if not any then
            done = true
        end
    end

    -- Finally build the final result list.
    local out = {}
    for i = 1, limit do
        local t = uniques[i]
        if not t then
            break
        end
        for j = 1, t.keep do
            table.insert(out, t[j])
        end
    end

    assert(#out <= limit)
    assert(#out <= total)

    return out
end

--------------------------------------------------------------------------------
-- Returns true when canceled; otherwise nil.
local function _do_suggest(line, lines, matches) -- luacheck: no unused
    -- Reset cancel flag.
    _cancel = nil

    local limit = clink._is_suggestionlist_mode() and 30 or nil
    local results = {}

    -- Protected call to suggesters.
    local impl = function(line, matches) -- luacheck: ignore 432
        local ran = {}
        local strategy = settings.get("autosuggest.strategy"):explode()

        if limit then
            -- Move "history" to the front so it always shows up first in the
            -- suggestion list (like in PSReadline for PowerShell).
            for index, name in ipairs(strategy) do
                if name == "history" then
                    table.remove(strategy, index)
                    table.insert(strategy, 1, name)
                    break
                end
            end
        end

        -- Call the strategies in order.
        for _, name in ipairs(strategy) do
            if not ran[name] then
                ran[name] = true
                local suggester = suggesters[name]
                if suggester then
                    local func = suggester.suggest
                    if func then
                        local s, o = func(suggester, line, matches, limit)
                        if _cancel then
                            return
                        end
                        if s ~= nil then
                            if type(s) ~= "table" then
                                s = { { s, o } }
                            end
                            if type(s) == "table" and s[1] then
                                local sub_results = {}
                                for _, e in ipairs(s) do
                                    local es = e[1] or e.suggestion or nil
                                    local eo = e[2] or e.offset or nil
                                    local eh = e.highlight
                                    local et = e.tooltip
                                    local ei = e.history
                                    if es then
                                        table.insert(sub_results, { es, eo, highlight=eh, tooltip=et, source=name, history=ei }) -- luacheck: no max line length
                                        if not limit then
                                            break
                                        end
                                    end
                                end
                                if sub_results[1] then
                                    table.insert(results, sub_results)
                                    if not limit then
                                        break
                                    end
                                end
                            end
                        end
                    end
                end
                if _cancel then
                    return
                end
            end
        end
    end

    local ok, ret = xpcall(impl, _error_handler_ret, line, matches)
    if not ok then
        print("")
        print("suggester failed:")
        print(ret)
        return
    end

    if _cancel then
        return true
    end

    if limit then
        results = aggregate(line, results, limit)
    else
        results = results[1]
    end

    local info = line:getwordinfo(line:getwordcount())
    clink.set_suggestion_result(line:getline(), info and info.offset or 1, results)
end

--------------------------------------------------------------------------------
local function deferred_generate(line, lines, matches, builder, generation_id)
    -- Cancel the current _do_suggest.
    _cancel = true

    -- Make sure volatile matches don't cause an infinite cycle.
    clink.set_suggestion_started(line:getline())

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
function clink._diag_suggesters(arg)
    arg = (arg and arg >= 2)
    if not arg and not settings.get("lua.debug") then
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
local function is_first_word(line)
    return line:getwordcount() <= 1 or line:getword(2) == ""
end

--------------------------------------------------------------------------------
local history_suggester = clink.suggester("history")
function history_suggester:suggest(line, matches, limit) -- luacheck: no unused
    return clink.history_suggester(line:getline(), is_first_word(line), limit, false)
end

--------------------------------------------------------------------------------
local prevcmd_suggester = clink.suggester("match_prev_cmd")
function prevcmd_suggester:suggest(line, matches, limit) -- luacheck: no unused
    return clink.history_suggester(line:getline(), is_first_word(line), limit, true)
end

--------------------------------------------------------------------------------
local completion_suggester = clink.suggester("completion")
function completion_suggester:suggest(line, matches, limit) -- luacheck: no unused
    local results = {}
    local count = line:getwordcount()
    if count > 0 then
        -- if limit and count == 1 then
        --     limit = 1
        -- end
        local info = line:getwordinfo(count)
        if info.offset < line:getcursor() then
            local typed = line:getline():sub(info.offset, line:getcursor())
            local no_quotes = matches:getsuppressquoting()
            local loop_max = limit or 99
            for i = 1, loop_max do
                local m = matches:getmatch(i)
                if not m then
                    break
                elseif m ~= typed and (limit or info.quoted or no_quotes or not rl.needquotes(m)) then
                    local append_quote = info.quoted
                    local hofs, matchlen = clink._find_match_highlight(m, typed)
                    hofs = hofs or 0
                    matchlen = matchlen or 0
                    hofs = hofs + info.offset
                    if limit and not info.quoted and not no_quotes and rl.needquotes(m) then
                        m = '"'..m
                        hofs = hofs + 1
                        append_quote = true
                    end
                    local append = matches:getappendchar(i)
                    if append and append ~= "" then
                        -- If append char and quoted, add closing quote.
                        if append_quote  then
                            m = m..'"'
                        end
                        -- Add append char.  This makes suggestions more
                        -- consistent with completion.  It also helps make
                        -- completion suggestions more likely to match history
                        -- suggestions so that the suggestion list's duplicate
                        -- removal works better.
                        m = m..append
                    end
                    local t = { m, info.offset, highlight={hofs, matchlen}, tooltip=matches:getdescription(i) }
                    table.insert(results, t)
                    if limit then
                        limit = limit - 1
                        if limit <= 0 then
                            break
                        end
                    else
                        break
                    end
                end
            end
        end
    end
    return results
end
