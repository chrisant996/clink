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
local function dir_match_generator(word)
    local matches = {}

    -- Find matches.
    for _, dir in ipairs(clink.find_dirs(word.."*")) do
        if clink.is_match(word, dir) then
            table.insert(matches, dir)
        end
    end

    return matches
end

--------------------------------------------------------------------------------
clink.arg.register_parser("cd", dir_match_generator)
clink.arg.register_parser("chdir", dir_match_generator)
clink.arg.register_parser("pushd", dir_match_generator)
clink.arg.register_parser("rd", dir_match_generator)
clink.arg.register_parser("rmdir", dir_match_generator)
clink.arg.register_parser("md", dir_match_generator)
clink.arg.register_parser("mkdir", dir_match_generator)
