-- Copyright (c) 2015 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink = clink or {}
local _generators = {}
local _generators_unsorted = false



--------------------------------------------------------------------------------
local function pcall_dispatch(func, ...)
    local ok, ret = pcall(func, ...)
    if not ok then
        print("")
        print(ret)
        print(debug.traceback())
        return
    end

    return ret
end

--------------------------------------------------------------------------------
local function prepare()
    -- Sort generators by priority if required.
    if _generators_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(_generators, lambda)

        _generators_unsorted = false
    end
end

--------------------------------------------------------------------------------
function clink._generate(line_state, match_builder)
    local impl = function ()
        for _, generator in ipairs(_generators) do
            local ret = generator:generate(line_state, match_builder)
            if ret == true then
                return true
            end
        end

        return false
    end

    prepare()
    local ret = pcall_dispatch(impl)
    return ret or false
end

--------------------------------------------------------------------------------
function clink._get_prefix_length(line_state)
    local impl = function ()
        local ret = 0
        for _, generator in ipairs(_generators) do
            if generator.getprefixlength then
                local i = generator:getprefixlength(line_state) or 0
                if i > ret then ret = i end
            end
        end

        return ret
    end

    prepare()
    local ret = pcall_dispatch(impl)
    return ret or 0
end

--------------------------------------------------------------------------------
--- -name:  clink.generator
--- -arg:   [priority:integer]
--- -ret:   table
function clink.generator(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(_generators, ret)

    _generators_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
--- -name:  clink.is_match
--- -arg:   needle
--- -arg:   candidate
--- Deprecated.  Old API shim; rxists only for backward compatibility, to
--- minimize the changes necessary to get old scripts working with the new API.
function clink.is_match(needle, candidate)
    return true
end

--------------------------------------------------------------------------------
--- -name:  clink.arg.new_parser
--- -arg:   ...
--- -ret:   table
--- Deprecated.  Old API shim; exists only for backward compatibility, to
--- minimize the changes necessary to get old scripts working with the new API.
clink.arg = clink.arg or {}
function clink.arg.new_parser(...)
    local p = clink.argmatcher():addarg(...)

    function p:add_flags(...)
        -- TODO: they don't seem to get added as flags
        return p:addflags(...)
    end

    return p
end

--------------------------------------------------------------------------------
--- -name:  clink.arg.register_parser
--- -arg:   cmd
--- -arg:   parser
--- Deprecated.  Old API shim; exists only for backward compatibility, to
--- minimize the changes necessary to get old scripts working with the new API.
function clink.arg.register_parser(cmd, parser)
    -- TODO: works if parser is a table; goes haywire if parser is a parser.
    clink.argmatcher(cmd):addarg(parser)
end
