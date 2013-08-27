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
local special_env_vars = {
    "cd", "date", "time", "random", "errorlevel",
    "cmdextversion", "cmdcmdline", "highestnumanodenumber"
}

--------------------------------------------------------------------------------
local function env_vars_display_filter(matches)
    local to_display = {}
    for _, m in ipairs(matches) do
        local _, _, out = m:find("(%%[^%%]+%%)$")
        table.insert(to_display, out)
    end

    return to_display
end

--------------------------------------------------------------------------------
local function env_vars_find_matches(candidates, prefix, part)
    local part_len = #part
    for _, name in ipairs(candidates) do
        if clink.lower(name:sub(1, part_len)) == part then
            clink.add_match(prefix..'%'..name:lower()..'%')
        end
    end
end

--------------------------------------------------------------------------------
local function env_vars_match_generator(text, first, last)
    local all = rl_line_buffer:sub(1, last)

    -- Skip pairs of %s
    local i = 1
    for _, r in function () return all:find("%b%%", i) end do
        i = r + 2
    end

    -- Find a solitary %
    local i = all:find("%%", i)
    if not i then
        return false
    end

    if i < first then
        return false
    end

    local part = clink.lower(all:sub(i + 1))
    local part_len = #part

    i = i - first
    local prefix = text:sub(1, i)

    env_vars_find_matches(clink.get_env_var_names(), prefix, part)
    env_vars_find_matches(special_env_vars, prefix, part)

    if clink.match_count() >= 1 then
        clink.match_display_filter = env_vars_display_filter

        clink.suppress_char_append()
        clink.suppress_quoting()

        return true
    end

    return false
end

--------------------------------------------------------------------------------
if clink.get_host_process() == "cmd.exe" then
    clink.register_match_generator(env_vars_match_generator, 10)
end

-- vim: expandtab
