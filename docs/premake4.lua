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
newaction {
    trigger = "clink_docs",
    description = "Generates Clink's documentation.",
    execute = function ()
        if not os.getenv("cli_env") then
            print("CLI_ENV not set...")
            return
        end

        local cmd = "pandoc"
        cmd = cmd.." --from=markdown"
        cmd = cmd.." --to=html"
        cmd = cmd.." --css=docs/clink.css"
        cmd = cmd.." --template=docs/template.html"
        cmd = cmd.." --toc"
        cmd = cmd.." --self-contained"
        cmd = cmd.." -o .build/docs/temp.html"
        cmd = cmd.." docs/clink.md"
        cmd = cmd.." changes"

        os.execute("1>nul 2>&1 mkdir .build\\docs")
        os.execute(cmd)

        local out = io.open(".build/docs/clink.html", "w")
        for i in io.lines(".build/docs/temp.html") do
            local j = i:gsub("CLINK_VERSION", tostring(clink_ver))
            out:write(j.."\n")
        end
        out:close()

        os.execute("del .build\\docs\\temp.html")
    end
}
