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
function set_match_generator(text, first, last)
    -- Only show directories if the command is 'dir', 'cd', or 'pushd'
    local leading = rl_line_buffer:sub(1, first - 1)
    local cmd = leading:match("^%s*([a-zA-Z]+)%s+")
    if not cmd then
        return false
    end

    -- Check it's the set command.
    cmd = cmd:lower()
    if cmd ~= "set" then
        return false
    end

    -- Skip this generator if first is in the rvalue.
    if leading:find("=") then
        return false;
    end

    -- Enumerate environment variables and check for potential matches.
    for _, name in ipairs(clink.get_env_var_names()) do
        if clink.is_match(text, name) then
            clink.add_match(name)
        end
    end

    -- If there was only one match, add a '=' on the end.
    if clink.match_count() == 1 then
        clink.set_match(1, clink.get_match(1).."=")
        clink.suppress_char_append()
    end

    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(set_match_generator, 40)
