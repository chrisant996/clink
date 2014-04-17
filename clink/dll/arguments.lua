--
-- Copyright (c) 2012 Martin Ridgers
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

--------------------------------------------------------------------------------
clink.arg = {}

--------------------------------------------------------------------------------
local parsers               = {}
local is_parser
local is_sub_parser
local new_sub_parser
local parser_go_impl
--local merge_parsers

local parser_meta_table     = {}
local sub_parser_meta_table = {}

--------------------------------------------------------------------------------
function parser_meta_table.__concat(lhs, rhs)
    if not is_parser(rhs) then
        error("Right-handside must be parser.", 2)
    end

    local t = type(lhs)
    if t == "table" then
        local ret = {}
        for _, i in ipairs(lhs) do
            table.insert(ret, i .. rhs)
        end

        return ret
    elseif t ~= "string" then
        error("Left-handside must be a string or a table.", 2)
    end

    return new_sub_parser(lhs, rhs)
end

--------------------------------------------------------------------------------
local function unfold_table(source, target)
    for _, i in ipairs(source) do
        if type(i) == "table" and getmetatable(i) == nil then
            unfold_table(i, target)
        else
            table.insert(target, i)
        end
    end
end

--------------------------------------------------------------------------------
local function parser_is_flag(parser, part)
    if part == nil then
        return false
    end

    local prefix = part:sub(1, 1)
    return prefix == "-" or prefix == "/"
end

--------------------------------------------------------------------------------
local function parser_add_arguments(parser, ...)
    
    -- This string commented out to prevent clearing of parser.arguments table
    -- With this, call of this function will add new arguments to parser
    -- instead of replacing them

    -- parser.arguments = {}

    for _, i in ipairs({...}) do
        -- Check all arguments are tables.
        if type(i) ~= "table" then
            error("All arguments to set_arguments() must be tables.", 2)
        end

        -- Only parsers are allowed to be specified without being wrapped in a
        -- containing table.
        if getmetatable(i) ~= nil then
            if is_parser(i) then
                table.insert(parser.arguments, i)
            else
                error("Tables can't have meta-tables.", 2)
            end
        else
            -- Expand out nested tables and insert into object's arguments table.
            local arguments = {}
            unfold_table(i, arguments)
            table.insert(parser.arguments, arguments)
        end
    end

    return parser
end

--------------------------------------------------------------------------------
local function parser_set_arguments(parser, ...)
    parser.arguments = {}
    parser_add_arguments(parser, ...)
    return parser
end

--------------------------------------------------------------------------------
local function parser_set_flags(parser, ...)
    local flags = {}
    unfold_table({...}, flags)

    -- Validate the specified flags.
    for _, i in ipairs(flags) do
        if is_sub_parser(i) then
            i = i.key
        end

        -- Check all flags are strings.
        if type(i) ~= "string" then
            error("All parser flags must be strings. Found "..type(i), 2)
        end

        -- Check all flags start with a - or a /
        if not parser:is_flag(i) then
            error("Flags must begin with a '-' or a '/'", 2)
        end
    end

    parser.flags = flags
    return parser
end

--------------------------------------------------------------------------------
local function parser_flatten_argument(parser, index, part)
    -- Sanity check the 'index' param to make sure it's valid.
    if type(index) == "number" then
        if index <= 0 or index > #parser.arguments then
            return parser.use_file_matching
        end
    end

    if part == nil then
        part = ""
    end

    -- index == nil is a special case that returns the parser's flags
    local opts = {}
    local arg_opts
    if index == nil then
        arg_opts = parser.flags
    else
        arg_opts = parser.arguments[index]
    end

    -- Convert each argument option into a string and collect them in a table.
    for _, i in ipairs(arg_opts) do
        if is_sub_parser(i) then
            table.insert(opts, i.key)
        else
            local t = type(i)
            if t == "function" then
                local results = i(part)
                if type(results) == "table" then
                    for _, j in ipairs(results) do
                        table.insert(opts, j)
                    end
                end
            elseif t == "string" or t == "number" then
                table.insert(opts, tostring(i))
            end
        end
    end

    return opts
end

--------------------------------------------------------------------------------
local function parser_go_args(parser, state)
    local exhausted_args = false
    local exhausted_parts = false

    local part = state.parts[state.part_index]
    local arg_index = state.arg_index
    local arg_opts = parser.arguments[arg_index]
    local arg_count = #parser.arguments

    -- Is the next argument a parser? Parse control directly on to it.
    if is_parser(arg_opts) then
        state.arg_index = 1
        return parser_go_impl(arg_opts, state)
    end

    -- Advance parts state.
    state.part_index = state.part_index + 1
    if state.part_index > #state.parts then
        exhausted_parts = true
    end

    -- Advance argument state.
    state.arg_index = arg_index + 1
    if arg_index > arg_count then
        exhausted_args = true
    end

    -- We've exhausted all available arguments. We either loop or we're done.
    if parser.loop_point > 0 and state.arg_index > arg_count then
        state.arg_index = parser.loop_point
        if state.arg_index > arg_count then
            state.arg_index = arg_count
        end
    end

    -- Is there some state to process?
    if not exhausted_parts and not exhausted_args then
        local exact = false
        for _, arg_opt in ipairs(arg_opts) do
            -- Is the argument a key to a sub-parser? If so then hand control
            -- off to it.
            if is_sub_parser(arg_opt) then
                if arg_opt.key == part then
                    state.arg_index = 1
                    return parser_go_impl(arg_opt.parser, state)
                end
            end

            -- Check so see if the part has an exact match in the agrument. Note
            -- that only string-type options are considered.
            if type(arg_opt) == "string" then
                exact = exact or arg_opt == part
            else
                exact = true
            end
        end

        -- If the parser's required to be precise then check here.
        if parser.precise and not exact then
            exhausted_args = true
        else
            return nil
        end
    end

    -- If we've no more arguments to traverse but there's still parts remaining
    -- then we start skipping arguments but keep going so that flags still get
    -- parsed (as flags have no position).
    if exhausted_args then
        state.part_index = state.part_index - 1

        if not exhausted_parts then
            if state.depth <= 1 then
                state.skip_args = true
                return
            end

            return parser.use_file_matching
        end
    end

    return parser:flatten_argument(arg_index, part)
end

--------------------------------------------------------------------------------
local function parser_go_flags(parser, state)
    local part = state.parts[state.part_index]

    -- Advance parts state.
    state.part_index = state.part_index + 1
    if state.part_index > #state.parts then
        return parser:flatten_argument()
    end

    for _, arg_opt in ipairs(parser.flags) do
        if is_sub_parser(arg_opt) then
            if arg_opt.key == part then
                local arg_index_cache = state.arg_index
                local skip_args_cache = state.skip_args

                state.arg_index = 1
                state.skip_args = false
                state.depth = state.depth + 1

                local ret = parser_go_impl(arg_opt.parser, state)
                if type(ret) == "table" then
                    return ret
                end

                state.depth = state.depth - 1
                state.skip_args = skip_args_cache
                state.arg_index = arg_index_cache
            end
        end
    end
end

--------------------------------------------------------------------------------
function parser_go_impl(parser, state)
    local has_flags = #parser.flags > 0

    while state.part_index <= #state.parts do
        local part = state.parts[state.part_index]
        local dispatch_func

        if has_flags and parser:is_flag(part) then
            dispatch_func = parser_go_flags
        elseif not state.skip_args then
            dispatch_func = parser_go_args
        end

        if dispatch_func ~= nil then
            local ret = dispatch_func(parser, state)
            if ret ~= nil then
                return ret
            end
        else
            state.part_index = state.part_index + 1
        end
    end

    return parser.use_file_matching
end

--------------------------------------------------------------------------------
local function parser_go(parser, parts)
    -- Validate 'parts'.
    if type(parts) ~= "table" then
        error("'Parts' param must be a table of strings ("..type(parts)..").", 2)
    else
        if #parts == 0 then
            part = { "" }
        end

        for i, j in ipairs(parts) do
            local t = type(parts[i])
            if t ~= "string" then
                error("'Parts' table can only contain strings; "..j.."="..t, 2)
            end
        end
    end

    local state = {
        arg_index = 1,
        part_index = 1,
        parts = parts,
        skip_args = false,
        depth = 1,
    }

    return parser_go_impl(parser, state)
end

--------------------------------------------------------------------------------
local function parser_dump(parser, depth)
    if depth == nil then
        depth = 0
    end

    function prt(depth, index, text)
        local indent = string.sub("                                 ", 1, depth)
        text = tostring(text)
        print(indent..depth.."."..index.." - "..text)
    end

    -- Print arguments
    local i = 0
    for _, arg_opts in ipairs(parser.arguments) do
        for _, arg_opt in ipairs(arg_opts) do
            if is_sub_parser(arg_opt) then
                prt(depth, i, arg_opt.key)
                arg_opt.parser:dump(depth + 1)
            else
                prt(depth, i, arg_opt)
            end
        end

        i = i + 1
    end

    -- Print flags
    for _, flag in ipairs(parser.flags) do
        prt(depth, "F", flag)
    end
end

--------------------------------------------------------------------------------
function parser_be_precise(parser)
    parser.precise = true
    return parser
end

--------------------------------------------------------------------------------
function is_parser(p)
    return type(p) == "table" and getmetatable(p) == parser_meta_table
end

--------------------------------------------------------------------------------
function is_sub_parser(sp)
    return type(sp) == "table" and getmetatable(sp) == sub_parser_meta_table
end

--------------------------------------------------------------------------------
local function get_sub_parser(argument, str)
    for _, arg in ipairs(argument) do
        if is_sub_parser(arg) then
            if arg.key == str then
                return arg.parser
            end
        end
    end
end

--------------------------------------------------------------------------------
function new_sub_parser(key, parser)
    local sub_parser = {}
    sub_parser.key = key
    sub_parser.parser = parser

    setmetatable(sub_parser, sub_parser_meta_table)
    return sub_parser
end

--------------------------------------------------------------------------------
local function parser_disable_file_matching(parser)
    parser.use_file_matching = false
    return parser
end

--------------------------------------------------------------------------------
local function parser_loop(parser, loop_point)
    if loop_point == nil or type(loop_point) ~= "number" then
        loop_point = 1
    end

    parser.loop_point = loop_point
    return parser
end

--------------------------------------------------------------------------------
function clink.arg.new_parser( ... )

    local parser = {}

    -- Methods
    parser.set_flags = parser_set_flags
    parser.set_arguments = parser_set_arguments
    parser.add_arguments = parser_add_arguments
    parser.dump = parser_dump
    parser.go = parser_go
    parser.flatten_argument = parser_flatten_argument
    parser.be_precise = parser_be_precise
    parser.disable_file_matching = parser_disable_file_matching
    parser.loop = parser_loop
    parser.is_flag = parser_is_flag

    -- Members.
    parser.flags = {}
    parser.arguments = {}
    parser.precise = false
    parser.use_file_matching = true
    parser.loop_point = 0

    setmetatable(parser, parser_meta_table)

    -- If any arguments provided, threat them as parser's arguments or flags
    if ... and #... > 0 then

        local arguments = {}
        local flags = {}
        
        for _, word in ipairs({...}) do
            if type(word) == "string" then table.insert(flags, word)
            elseif type(word) == "table" then table.insert(arguments, word) end
        end

        for _, a in ipairs(arguments) do parser:add_arguments(a) end
        parser:set_flags(flags)

    end

    return parser
end

--------------------------------------------------------------------------------
function merge_parsers(lhs, rhs)
    -- Merging parsers is not a trivial matter and this implementation is far
    -- from correct. It is however sufficient for the majority of cases.

    -- Remove (and save value of) the first argument in RHS.
    local rhs_arg_1 = table.remove(rhs.arguments, 1)
    if rhs_arg_1 == nil then
        return
    end

    -- Get reference to the LHS's first argument table (creating it if needed).
    local lhs_arg_1 = lhs.arguments[1]
    if lhs_arg_1 == nil then
        lhs_arg_1 = {}
        table.insert(lhs.arguments, lhs_arg_1)
    end

    -- Link RHS to LHS through sub-parsers.
    for _, rarg in ipairs(rhs_arg_1) do
        local child

        -- Split sub parser
        if is_sub_parser(rarg) then
            child = rarg.parser     
            rarg = rarg.key
        else
            child = rhs
        end

        -- If LHS's first argument has rarg in it which links to a sub-parser
        -- then we need to recursively merge them.
        local lhs_sub_parser = get_sub_parser(lhs_arg_1, rarg)
        if lhs_sub_parser then
            merge_parsers(lhs_sub_parser, child)
        else
            local to_add = rarg
            if type(rarg) ~= "function" then
                to_add = rarg .. child
            end

            table.insert(lhs_arg_1, to_add)
        end
    end

    -- Merge flags.
    for _, rflag in ipairs(rhs.flags) do
        table.insert(lhs.flags, rflag)
    end
end

--------------------------------------------------------------------------------
function clink.arg.register_parser(cmd, parser)
    if not is_parser(parser) then
        local p = clink.arg.new_parser()
        p:set_arguments({ parser })
        parser = p
    end

    cmd = cmd:lower()
    local prev = parsers[cmd]
    if prev ~= nil then
        merge_parsers(prev, parser)
    else
        parsers[cmd] = parser
    end
end

--------------------------------------------------------------------------------
local function argument_match_generator(text, first, last)
    local leading = rl_state.line_buffer:sub(1, first - 1):lower()

    -- Extract the command.
    local cmd_l, cmd_r
    if leading:find("^%s*\"") then
        -- Command appears to be surround by quotes.
        cmd_l, cmd_r = leading:find("%b\"\"")
        if cmd_l and cmd_r then
            cmd_l = cmd_l + 1
            cmd_r = cmd_r - 1
        end
    else
        -- No quotes so the first, longest, non-whitespace word is extracted.
        cmd_l, cmd_r = leading:find("[^%s]+")
    end

    if not cmd_l or not cmd_r then
        return false
    end

    local regex = "[\\/:]*([^\\/:.]+)(%.*[%l]*)%s*$"
    local _, _, cmd, ext = leading:sub(cmd_l, cmd_r):lower():find(regex)

    -- Check to make sure the extension extracted is in pathext.
    if ext and ext ~= "" then
        if not clink.get_env("pathext"):lower():match(ext.."[;$]", 1, true) then
            return false
        end
    end
    
    -- Find a registered parser.
    local parser = parsers[cmd]
    if parser == nil then
        return false
    end

    -- Split the command line into parts.
    local str = rl_state.line_buffer:sub(cmd_r + 2, last)
    local parts = {}
    for _, sub_str in ipairs(clink.quote_split(str, "\"")) do
        -- Quoted strings still have their quotes. Look for those type of
        -- strings, strip the quotes and add it completely.
        if sub_str:sub(1, 1) == "\"" then
            local l, r = sub_str:find("\"[^\"]+")
            if l then
                local part = sub_str:sub(l + 1, r)
                table.insert(parts, part)
            end
        else
            -- Extract non-whitespace parts.
            for _, r, part in function () return sub_str:find("^%s*([^%s]+)") end do
                table.insert(parts, part)
                sub_str = sub_str:sub(r + 1)
            end
        end
    end

    -- If 'text' is empty then add it as a part as it would have been skipped
    -- by the split loop above.
    if text == "" then
        table.insert(parts, text)
    end

    -- Extend rl_state with match generation state; text, first, and last.
    rl_state.text = text
    rl_state.first = first
    rl_state.last = last

    -- Call the parser.
    local needle = parts[#parts]
    local ret = parser:go(parts)
    if type(ret) ~= "table" then
        return not ret
    end

    -- Iterate through the matches the parser returned and collect matches.
    for _, match in ipairs(ret) do
        if clink.is_match(needle, match) then
            clink.add_match(match)
        end
    end

    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(argument_match_generator, 25)

-- vim: expandtab
