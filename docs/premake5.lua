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
local function generate_file(source_path, out)
    print("  << " .. source_path)
    for line in io.open(source_path, "r"):lines() do
        local include = line:match("%$%(INCLUDE +([^)]+)%)")
        if include then
            include = path.getdirectory(source_path) .. "/" .. include
            generate_file(include, out)
        else
            line = line:gsub("%$%(CLINK_VERSION%)", tostring(clink_ver))
            out:write(line .. "\n")
        end
    end
end

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_docs",
    description = "Generates Clink's documentation.",
    execute = function ()
        out_path = ".build/docs/clink.html"

        os.execute("1>nul 2>nul md .build\\docs")

        print("")
        print(">> " .. out_path)
        generate_file("docs/clink.html", io.open(out_path, "w"))
        print("")
    end
}
