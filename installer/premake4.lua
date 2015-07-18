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
local function warn(msg)
    print("WARNING: " .. msg)
end

--------------------------------------------------------------------------------
local function exec(cmd)
    -- Helper funciton to show executed commands to TTY
    print("## EXEC: " .. cmd:gsub("1>nul ", ""):gsub("2>nul ", ""))
    return os.execute(cmd)
end

--------------------------------------------------------------------------------
local function mkdir(dir)
    if os.isdir(dir) then
        return
    end

    local ret = exec("1>nul 2>nul md " .. path.translate(dir))
    if ret ~= 0 then
        error("Failed to create directory '" .. dir .. "'")
    end
end

--------------------------------------------------------------------------------
local function rmdir(dir)
    if not os.isdir(dir) then
        return
    end

    return exec("1>nul 2>nul rd /q /s " .. path.translate(dir))
end

--------------------------------------------------------------------------------
local function unlink(file)
    return exec("1>nul 2>nul del /q " .. path.translate(file))
end

--------------------------------------------------------------------------------
local function copy(src, dest)
    src = path.translate(src)
    dest = path.translate(dest)
    return exec("1>nul 2>nul copy /y " .. src .. " " .. dest)
end

--------------------------------------------------------------------------------
local function concat(one, two)
    one = path.translate(one)
    two = path.translate(two)
    exec("2>nul type " .. one .. " " .. two .. " 1>MR")
    copy("MR", one)
    unlink("MR")
end

--------------------------------------------------------------------------------
local function have_required_tool(name)
    return (exec("1>nul 2>nul where " .. name) == 0)
end

--------------------------------------------------------------------------------
local function get_target_dir(nightly)
    local target_dir

    if nightly then
        target_dir = tostring(nightly)
        target_dir = target_dir .. os.date("%Y%m%d_")
        target_dir = target_dir .. get_last_git_commit()
    else
        target_dir = ".build/release/"
        if clink_ver ~= "DEV" then
            target_dir = target_dir .. os.date("%Y%m%d_%H%M%S_")
        end
        target_dir = target_dir .. clink_ver
    end

    target_dir = path.getabsolute(target_dir) .. "/"
    if not os.isdir(target_dir .. ".") then
        rmdir(target_dir)
        mkdir(target_dir)
    end

    return target_dir
end

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_release",
    description = "Creates a release of Clink.",
    execute = function ()
        local premake = _PREMAKE_COMMAND
        local target_dir = get_target_dir()
        local git_checkout = clink_ver

        clink_ver = clink_ver:upper()

        -- Check we have the tools we need.
        local have_msbuild = have_required_tool("msbuild")
        local have_mingw = have_required_tool("mingw32-make")
        local have_nsis = have_required_tool("makensis")
        local have_7z = have_required_tool("7z")

        -- If we're not building DEV, create a clone and checkout correct version
        -- and build it.
        if clink_ver ~= "DEV" then
            local repo_path = "clink_" .. clink_ver .. "_src"
            local code_dir = target_dir .. repo_path
            if not os.isdir(code_dir .. ".") then
                mkdir(code_dir)
            end

            -- clone repro in release folder and checkout the specified version
            exec("git clone . " .. code_dir)
            if not os.chdir(code_dir) then
                print("Failed to chdir to '" .. code_dir .. "'")
                return
            end
            exec("git checkout " .. git_checkout)
        end
        local src_dir_name = path.getabsolute(".")

        -- Build the code.
        local x86_ok = true;
        local x64_ok = true;
        local toolchain = "ERROR"
        if have_msbuild then
            toolchain = _OPTIONS["clink_vs_ver"] or "vs2013"
            exec(premake .. " --clink_ver=" .. clink_ver .. " " .. toolchain)

            ret = exec("msbuild /m /v:q /p:configuration=release /p:platform=win32 .build/" .. toolchain .. "/clink.sln")
            if ret ~= 0 then
                x86_ok = false
            end

            ret = exec("msbuild /m /v:q /p:configuration=release /p:platform=x64 .build/" .. toolchain .. "/clink.sln")
            if ret ~= 0 then
                x64_ok = false
            end
        elseif have_mingw then
            toolchain = "gmake"
            exec(premake .. " --clink_ver=" .. clink_ver .. " gmake")
            os.chdir(".build/gmake")

            local ret
            ret = exec("1>nul mingw32-make CC=gcc config=release_x32 -j%number_of_processors%")
            if ret ~= 0 then
                x86_ok = false
            end

            ret = exec("1>nul mingw32-make CC=gcc config=release_x64 -j%number_of_processors%")
            if ret ~= 0 then
                x64_ok = false
            end

            os.chdir("../..")
        else
            error("Unable to locate either msbuild.exe or mingw32-make.exe.")
        end

        local src = ".build/" .. toolchain .. "/bin/release/"
        local dest = target_dir .. "clink_" .. clink_ver

        -- Do a coarse check to make sure there's a build available.
        print(x86_ok)
        print(x64_ok)
        if not os.isdir(src .. ".") or not (x86_ok or x64_ok) then
            print("There's no build available in '" .. src .. "'")
            return
        end

        -- Copy release files to a directory.
        local manifest = {
            "clink.bat",
            "clink_x*.exe",
            "clink*.dll",
            "*.lua",
            "clink_inputrc_base",
            "CHANGES",
            "LICENSE",
            "clink_dll_x*.pdb",
        }

        mkdir(dest)
        for _, mask in ipairs(manifest) do
            copy(src .. mask, dest)
        end

        -- Lump lua files together.
        unlink(dest .. "/clink._lua")
        concat(dest .. "/clink._lua", dest .. "/clink.lua")
        concat(dest .. "/clink._lua", dest .. "/arguments.lua")
        concat(dest .. "/clink._lua", dest .. "/debugger.lua")
        unlink(dest .. "/clink.lua")
        unlink(dest .. "/arguments.lua")
        unlink(dest .. "/debugger.lua")

        local lua_lump = io.open(dest .. "/clink._lua", "a")
        for _, i in ipairs(os.matchfiles(dest .. "/*.lua")) do
            i = path.translate(i)
            print("lumping " .. i)

            lua_lump:write("\n--------------------------------------------------------------------------------\n")
            lua_lump:write("-- ", path.getname(i), "\n")
            lua_lump:write("--\n\n")

            for l in io.lines(i, "r") do
                lua_lump:write(l, "\n")
            end
        end
        lua_lump:close()

        unlink(dest .. "/*.lua")
        copy(dest .. "/clink._lua", dest .. "/clink.lua")
        unlink(dest .. "/clink._lua")

        -- Generate documentation.
        exec(premake .. " --clink_ver=" .. clink_ver .. " clink_docs")
        copy(".build/docs/clink.html", dest)

        -- Build the installer.
        if have_nsis then
            local nsis_cmd = "makensis"
            nsis_cmd = nsis_cmd .. " /DCLINK_BUILD=" .. dest
            nsis_cmd = nsis_cmd .. " /DCLINK_VERSION=" .. clink_ver
            nsis_cmd = nsis_cmd .. " /DCLINK_SOURCE=" .. src_dir_name
            nsis_cmd = nsis_cmd .. " " .. src_dir_name .. "/installer/clink.nsi"
            exec(nsis_cmd)
        end

        -- Tidy up code directory.
        if clink_ver ~= "DEV" and not nightly then
            rmdir(".build")
            rmdir(".git")
            unlink(".gitignore")

            -- Zip up the source code.
            os.chdir(target_dir)
            if have_7z then
                exec("7z a -r " .. target_dir .. "clink_" .. clink_ver .. "_src.zip " .. src_dir_name)
            end
            rmdir(src_dir_name)
        end

        -- Move PDBs out of the way and zip them up.
        os.chdir(dest)
        if have_msbuild then
            exec("move *.pdb  .. ")
            if have_7z then
                exec("7z a -r  ../clink_" .. clink_ver .. "_pdb.zip  ../*.pdb")
                unlink("../*.pdb")
            end
        end

        -- Package the release up in an archive.
        if have_7z then
            exec("7z a -r  ../clink_" .. clink_ver .. ".zip  ../clink_" .. clink_ver)
        end

        -- Stuff...
        if nightly then
            os.chdir(dest .. "/..")
            rmdir(dest)
        end

        -- Report some facts about what just happened.
        print("\n\n")
        if not have_7z then     warn("7-ZIP NOT FOUND     -- Packing to .zip files was skipped.") end
        if not have_nsis then   warn("NSIS NOT FOUND      -- No installer was not created.") end
        if not x86_ok then      warn("x86 BUILD FAILED") end
        if not x64_ok then      warn("x64 BUILD FAILED") end
    end
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "clink_vs_ver",
   value       = "VER",
   description = "Version of Visual Studio to build release with."
}

-- vim: expandtab
