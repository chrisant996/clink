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
    description = "Generates clink's documentation.",
    execute = function ()
        if not os.getenv("cli_env") then
            print("CLI_ENV not set...")
            return
        end

        local cmd = "python %cli_env%\\asciidoc-8.6.8\\asciidoc.py"
        cmd = cmd.." -o .build/docs/clink.html"
        cmd = cmd.." --theme clink"
        cmd = cmd.." --attribute=CLINK_VERSION="..clink_ver
        cmd = cmd.." --attribute=toc"
        cmd = cmd.." -v"
        cmd = cmd.." docs\\clink.asciidoc"

        os.execute("mkdir .build\\docs 1>nul 2>nul")
        os.execute(cmd)
    end
}
