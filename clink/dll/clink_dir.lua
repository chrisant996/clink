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
local function dir_match_generator(text, first, last)
    -- Strip off any path components that may be on text.
    local prefix = ""
    local i = text:find("[\\/:][^\\/:]*$")
    if i then
        prefix = text:sub(1, i)
    end

    local mask = text.."*"

    -- If readline's -/_ mapping is on then adjust mask.
    if clink.is_rl_variable_true("completion-map-case") then
        local function mangle_mask(m)
            return m:gsub("_", "?"):gsub("-", "?")
        end

        local sep = mask:reverse():find("\\", 2)
        if sep ~= nil then
            sep = #mask - sep + 1;

            local mask_left = mask:sub(1, sep)
            local mask_right = mask:sub(sep + 1)

            mask = mask_left..mangle_mask(mask_right)
        else
            mask = mangle_mask(mask)
        end
    end

    -- Find matches.
    for _, dir in ipairs(clink.find_dirs(mask)) do
        if not dir:find("^%.+$") then
            local file = prefix..dir
            if clink.is_match(text, file) then
                clink.add_match(prefix..dir)
            end
        end
    end

    -- If there was no matches but text is a dir then use it as the single match.
    -- Otherwise tell readline that matches are files and it will do magic.
    if clink.match_count() == 0 then
        if clink.is_dir(text) then
            clink.add_match(text)
        end
    else
        clink.matches_are_files()
    end

    return true
end

--------------------------------------------------------------------------------
clink.arg.register_tree("cd", dir_match_generator)
clink.arg.register_tree("chdir", dir_match_generator)
clink.arg.register_tree("dir", dir_match_generator)
clink.arg.register_tree("pushd", dir_match_generator)
clink.arg.register_tree("rd", dir_match_generator)
clink.arg.register_tree("rmdir", dir_match_generator)
clink.arg.register_tree("md", dir_match_generator)
clink.arg.register_tree("mkdir", dir_match_generator)
