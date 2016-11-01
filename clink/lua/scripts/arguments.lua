-- Copyright (c) 2016 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local _arglink = {}
_arglink.__index = _arglink
setmetatable(_arglink, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _arglink._new(key, matcher)
    return setmetatable({
        _key = key,
        _matcher = matcher,
    }, _arglink)
end



--------------------------------------------------------------------------------
local _argreader = {}
_argreader.__index = _argreader
setmetatable(_argreader, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _argreader._new(root)
    local reader = setmetatable({
        _matcher = root,
        _arg_index = 1,
        _stack = {},
    }, _argreader)
    return reader
end

--------------------------------------------------------------------------------
function _argreader:update(word)
    -- Check for flags and swtich matcher if the word is a flag.
    local matcher = self._matcher
    local is_flag = matcher:_is_flag(word)
    if is_flag then
        if matcher._flags then
            self:_push(matcher._flags)
        else
            return
        end
    end

    matcher = self._matcher
    local arg_index = self._arg_index
    local arg = matcher._args[arg_index]

    arg_index = arg_index + 1

    -- If arg_index is out of bounds we should loop if set or return to the
    -- previous matcher if possible.
    if arg_index > #matcher._args then
        if matcher._loop then
            self._arg_index = math.min(math.max(matcher._loop, 1), #matcher._args)
        elseif not self:_pop() then
            self._arg_index = arg_index
        end
    else
        self._arg_index = arg_index
    end

    -- Some matchers have no args at all.
    if not arg then
        return
    end

    -- Does the word lead to another matcher?
    for key, linked in pairs(arg._links) do
        if key == word then
            self:_push(linked)
            break
        end
    end
end

--------------------------------------------------------------------------------
function _argreader:_push(matcher)
    table.insert(self._stack, { self._matcher, self._arg_index })
    self._matcher = matcher
    self._arg_index = 1
end

--------------------------------------------------------------------------------
function _argreader:_pop()
    if #self._stack > 0 then
        self._matcher, self._arg_index = table.unpack(table.remove(self._stack))
        return true
    end

    return false
end



--------------------------------------------------------------------------------
local _argmatcher = {}
_argmatcher.__index = _argmatcher
setmetatable(_argmatcher, { __call = function (x, ...) return x._new(...) end })

--------------------------------------------------------------------------------
function _argmatcher._new()
    local matcher = setmetatable({
        _args = {},
    }, _argmatcher)
    matcher:setflagprefix("-")
    return matcher
end

--------------------------------------------------------------------------------
function _argmatcher:addarg(...)
    local list = { _links = {} }
    self:_add(list, {...})
    table.insert(self._args, list)
    return self
end

--------------------------------------------------------------------------------
function _argmatcher:addflags(...)
    local flag_matcher = self._flags or _argmatcher()
    local list = flag_matcher._args[1] or { _links = {} }
    flag_matcher:_add(list, {...})

    flag_matcher._args[1] = list
    self._flags = flag_matcher
    return self
end

--------------------------------------------------------------------------------
function _argmatcher:loop(index)
    self._loop = index or -1
    return self
end

--------------------------------------------------------------------------------
function _argmatcher:setflagprefix(...)
    local input = {...}
    if #input > 0 then
        for _, i in ipairs(input) do
            if type(i) ~= "string" or #i ~= 1 then
                error("Flag prefixes must be single character strings", 2)
            end
        end
        self._flagprefix = input
    end

    return self
end

--------------------------------------------------------------------------------
function _argmatcher:nofiles()
    self._no_file_generation = true
    return self
end

--------------------------------------------------------------------------------
function _argmatcher.__concat(lhs, rhs)
    if getmetatable(rhs) ~= _argmatcher then
        error("Right-hand side must be an argmatcher object", 2)
    end

    local t = type(lhs)
    if t == "string" then
        return _arglink(lhs, rhs)
    end

    if t == "table" then
        local ret = {}
        for _, i in ipairs(lhs) do
            table.insert(ret, i .. rhs)
        end
        return ret
    end

    error("Left-hand side must be a string or a table of strings", 2)
end

--------------------------------------------------------------------------------
function _argmatcher:__call(arg)
    if type(arg) ~= "table" then
        error("Shorthand matcher arguments must be tables", 2)
    end

    local is_flag
    is_flag = function(x)
        local is_link = (getmetatable(x) == _arglink)
        if type(x) == "table" and not is_link then
            return is_flag(x[1])
        end

        if is_link then
            x = x._key
        end

        return self:_is_flag(tostring(x))
    end

    if is_flag(arg[1]) then
        return self:addflags(table.unpack(arg))
    end

    return self:addarg(table.unpack(arg))
end

--------------------------------------------------------------------------------
function _argmatcher:_is_flag(word)
    local first_char = word:sub(1, 1)
    for _, i in ipairs(self._flagprefix) do
        if first_char == i then
            return true
        end
    end

    return false
end

--------------------------------------------------------------------------------
function _argmatcher:_add(list, addee)
    -- Flatten out tables unless the table is a link
    local is_link = (getmetatable(addee) == _arglink)
    if type(addee) == "table" and not is_link and not addee.match then
        for _, i in ipairs(addee) do
            self:_add(list, i)
        end
        return
    end

    if is_link then
        list._links[addee._key] = addee._matcher
    else
        table.insert(list, addee)
    end
end

--------------------------------------------------------------------------------
function _argmatcher:_generate(line_state, match_builder)
    local reader = _argreader(self)

    -- Consume words and use them to move through matchers' arguments.
    local word_count = line_state:getwordcount()
    for word_index = 2, (word_count - 1) do
        local word = line_state:getword(word_index)
        reader:update(word)
    end

    -- There should always be a matcher left on the stack, but the arg_index
    -- could be well out of range.
    local matcher = reader._matcher
    local arg_index = reader._arg_index

    -- Are we left with a valid argument that can provide matches?
    local add_matches = function(arg)
        for key, _ in pairs(arg._links) do
            match_builder:addmatch(key)
        end

        for _, i in ipairs(arg) do
            if type(i) == "function" then
                -- TODO: Some sort of index breadcrumb instead of word_index?
                match_builder:addmatches(i(word_count, line_state))
            else
                match_builder:addmatch(i)
            end
        end
    end

    if matcher._flags then
        add_matches(matcher._flags._args[1])
    end

    --if not matcher._args then pause() end
    local arg = matcher._args[arg_index]
    if arg then
        add_matches(arg)
        return true
    end

    -- No valid argument. Decide if we should match files or not.
    local no_files = matcher._no_file_generation or #matcher._args == 0
    return no_files
end



--------------------------------------------------------------------------------
clink._argmatchers = {}

--------------------------------------------------------------------------------
function clink:argmatcher(...)
    local matcher = _argmatcher()

    -- Extract priority from the arguments.
    matcher._priority = 999
    local input = {...}
    if #input > 0 and type(input[1]) == "number" then
        matcher._priority = input[1]
        table.remove(input, 1)
    end

    -- Register the argmatcher
    for _, i in ipairs(input) do
        self._argmatchers[i:lower()] = matcher
    end

    return matcher
end



--------------------------------------------------------------------------------
local argmatcher_generator = clink:generator(24)

function argmatcher_generator:generate(line_state, match_builder)
    -- Running and argmatcher only makes sense if there's two or more words.
    if line_state:getwordcount() < 2 then
        return false
    end

    local first_word = line_state:getword(1)

    -- Search for a valid argmatcher and call it.
    local argmatcher_keys = {
        path.getname(first_word):lower(),
        path.getbasename(first_word):lower(),
    }

    for _, key in ipairs(argmatcher_keys) do
        local argmatcher = clink._argmatchers[key]
        if argmatcher then
            return argmatcher:_generate(line_state, match_builder)
        end
    end

    return false
end
