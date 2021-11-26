-- Copyright (c) 2021 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local suggesters = {}



--------------------------------------------------------------------------------
local function _do_suggest(line, matches)
    -- Protected call to suggesters.
    local impl = function(line, matches)
        local suggested, onwards
        local strategy = settings.get("autosuggest.strategy"):explode()
        for _, name in ipairs(strategy) do
            local suggester = suggesters[name]
            if suggester then
                local func = suggester.suggest
                if func then
                    suggested, onwards = func(suggester, line, matches)
                    if suggested ~= nil or onwards == false then
                        return suggested
                    end
                end
            end
        end
    end

    local ok, ret = xpcall(impl, _error_handler_ret, line, matches)
    if not ok then
        print("")
        print("suggester failed:")
        print(ret)
        return false
    end

    return ret
end

--------------------------------------------------------------------------------
function clink._suggest(line, matches)
    return _do_suggest(line, matches)
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
--- and a string argument which contains the longest common subsequence of the
--- current set of possible completions.  The function can return nil to give
--- the next suggester a chance, or can return a suggestion (or an empty string)
--- to stop looking for suggestions.  See
--- <a href="#customisingsuggestions">Customizing Suggestions</a> for more
--- information.
--- -show:  local doskeyarg = clink.suggester("doskeyarg")
--- -show:  function doskeyarg:suggest(line, lcd)
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

    local ret = {}
    suggesters[name] = ret

    return ret
end



--------------------------------------------------------------------------------
local function suffix(line, suggestion, minlen)
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
function history_suggester:suggest(line, matches)
    return clink.history_suggester(line:getline(), false)
end

--------------------------------------------------------------------------------
local prevcmd_suggester = clink.suggester("match_prev_cmd")
function prevcmd_suggester:suggest(line, matches)
    return clink.history_suggester(line:getline(), true)
end

--------------------------------------------------------------------------------
local completion_suggester = clink.suggester("completion")
function completion_suggester:suggest(line, matches)
    return suffix(line, matches:getmatch(1), 1)
end
