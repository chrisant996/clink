-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function warn(msg)
    print("WARNING: " .. msg)
end

--------------------------------------------------------------------------------
local function exec(cmd, silent)
    print("\n\n## EXEC: " .. cmd)

    if silent then
        cmd = "1>nul 2>nul "..cmd
    else
        cmd = "1>nul "..cmd
    end

    -- Premake replaces os.execute() with a version that runs path.normalize()
    -- which converts \ to /. This is fine for everything except cmd.exe.
    local prev_norm = path.normalize
    path.normalize = function (x) return x end
    local _, _, ret = os.execute(cmd)
    path.normalize = prev_norm

    return ret == 0
end

--------------------------------------------------------------------------------
local function mkdir(dir)
    if os.isdir(dir) then
        return
    end

    local ret = exec("md " .. path.translate(dir), true)
    if not ret then
        error("Failed to create directory '" .. dir .. "' ("..tostring(ret)..")", 2)
    end
end

--------------------------------------------------------------------------------
local function rmdir(dir)
    if not os.isdir(dir) then
        return
    end

    return exec("rd /q /s " .. path.translate(dir), true)
end

--------------------------------------------------------------------------------
local function unlink(file)
    return exec("del /q " .. path.translate(file), true)
end

--------------------------------------------------------------------------------
local function copy(src, dest)
    src = path.translate(src)
    dest = path.translate(dest)
    return exec("copy /y " .. src .. " " .. dest, true)
end

--------------------------------------------------------------------------------
local function have_required_tool(name, fallback)
    local tool = exec("where " .. name, true)
    if not tool and fallback then
        local t
        if type(fallback) == "table" then
            t = fallback
        else
            t = { fallback }
        end
        for _,dir in ipairs(t) do
            tool = exec('where /r "'..dir..'" '..name, true)
            if tool then
                break
            end
        end
    end
    return tool
end

--------------------------------------------------------------------------------
newaction {
    trigger = "release",
    description = "Creates a release of Clink.",
    execute = function ()
        local premake = _PREMAKE_COMMAND
        local root_dir = path.getabsolute(".build/release").."/"

        -- Check we have the tools we need.
        local have_msbuild = have_required_tool("msbuild")
        local have_mingw = have_required_tool("mingw32-make")
        local have_nsis = have_required_tool("makensis", "c:\\Program Files (x86)\\NSIS")
        local have_7z = have_required_tool("7z", { "c:\\Program Files\\7-Zip", "c:\\Program Files (x86)\\7-Zip" })

        -- Clone repo in release folder and checkout the specified version
        local code_dir = root_dir.."~working/"
        rmdir(code_dir)
        mkdir(code_dir)

        exec("git clone . " .. code_dir)
        if not os.chdir(code_dir) then
            error("Failed to chdir to '" .. code_dir .. "'")
        end
        exec("git checkout " .. (_OPTIONS["commit"] or "HEAD"))

        -- Build the code.
        local x86_ok = true
        local x64_ok = true
        local toolchain = "ERROR"
        local build_code = function (target)
            if have_msbuild then
                target = target or "build"

                toolchain = _OPTIONS["vsver"] or "vs2019"
                exec(premake .. " " .. toolchain)
                os.chdir(".build/" .. toolchain)

                x86_ok = exec("msbuild /m /v:q /p:configuration=final /p:platform=win32 clink.sln /t:" .. target)
                x64_ok = exec("msbuild /m /v:q /p:configuration=final /p:platform=x64 clink.sln /t:" .. target)

                os.chdir("../..")
            elseif have_mingw then
                target = target or "build"

                toolchain = "gmake"
                exec(premake .. " " .. toolchain)
                os.chdir(".build/" .. toolchain)

                x86_ok = exec("mingw32-make CC=gcc config=final_x32 -j%number_of_processors% " .. target)
                x64_ok = exec("mingw32-make CC=gcc config=final_x64 -j%number_of_processors% " .. target)

                os.chdir("../..")
            else
                error("Unable to locate either msbuild.exe or mingw32-make.exe")
            end
        end

        -- Build everything.
        build_code("luac")
        exec(premake .. " embed")
        build_code()

        local src = path.getabsolute(".build/" .. toolchain .. "/bin/final").."/"

        -- Do a coarse check to make sure there's a build available.
        if not os.isdir(src .. ".") or not (x86_ok or x64_ok) then
            error("There's no build available in '" .. src .. "'")
        end

        -- Run tests.
        if x86_ok then
            test_exe = path.translate(src.."/clink_test_x86.exe")
            if not exec(test_exe) then
                error("x86 tests failed")
            end
        end

        if x64_ok then
            test_exe = path.translate(src.."/clink_test_x64.exe")
            if not exec(test_exe) then
                error("x64 tests failed")
            end
        end

        -- Now we can extract the version from the executables.
        local version = nil
        local clink_exe = x86_ok and "clink_x86.exe" or "clink_x64.exe"
        local ver_cmd = src:gsub("/", "\\")..clink_exe.." --version"
        for line in io.popen(ver_cmd):lines() do
            version = line
        end
        if not version then
            error("Failed to extract version from build executables")
        end

        -- Now we know the version we can create our output directory.
        local target_dir = root_dir..os.date("%Y%m%d_%H%M%S").."_"..version.."/"
        rmdir(target_dir)
        mkdir(target_dir)

        local clink_suffix = "clink-"..version
        local dest = target_dir..clink_suffix.."/"
        mkdir(dest)

        -- Copy release files to a directory.
        local manifest = {
            "clink.bat",
            "clink.lua",
            "clink_x*.exe",
            "clink*.dll",
            "clink*.pdb",
            "CHANGES",
            "LICENSE",
            "clink_x*.pdb",
            "clink_dll_x*.pdb",
        }

        for _, mask in ipairs(manifest) do
            copy(src .. mask, dest)
        end

        -- Generate documentation.
        exec(premake .. " docs")
        copy(".build/docs/clink.html", dest)

        -- Build the installer.
        local nsis_ok = false
        if have_nsis then
            local nsis_cmd = "makensis"
            nsis_cmd = nsis_cmd .. " /DCLINK_BUILD=" .. path.getabsolute(dest)
            nsis_cmd = nsis_cmd .. " /DCLINK_VERSION=" .. version
            nsis_cmd = nsis_cmd .. " /DCLINK_SOURCE=" .. code_dir
            nsis_cmd = nsis_cmd .. " " .. code_dir .. "/installer/clink.nsi"
            nsis_ok = exec(nsis_cmd)
        end

        -- Tidy up code directory.
        rmdir(".build")
        rmdir(".git")
        unlink(".gitignore")

        -- Zip up the source code.
        os.chdir("..")
        local src_dir_name = target_dir..clink_suffix.."_src"
        exec("move ~working "..src_dir_name)

        os.chdir(target_dir)
        if have_7z then
            exec("7z a -r " .. target_dir .. clink_suffix .. "_src.zip " .. src_dir_name)
        end
        rmdir(src_dir_name)

        -- Move PDBs out of the way and zip them up.
        os.chdir(dest)
        if have_msbuild then
            exec("move *.pdb  .. ")
            if have_7z then
                exec("7z a -r  ../"..clink_suffix .. "_pdb.zip  ../*.pdb")
                unlink("../*.pdb")
            end
        end

        -- Package the release in an archive.
        if have_7z then
            exec("7z a -r  ../"..clink_suffix .. ".zip  ../"..clink_suffix)
        end

        -- Report some facts about what just happened.
        print("\n\n")
        if not have_7z then     warn("7-ZIP NOT FOUND -- Packing to .zip files was skipped.") end
        if not have_nsis then   warn("NSIS NOT FOUND -- No installer was not created.") end
        if not nsis_ok then     warn("INSTALLER PACKAGE FAILED") end
        if not x86_ok then      warn("x86 BUILD FAILED") end
        if not x64_ok then      warn("x64 BUILD FAILED") end
    end
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "vsver",
   value       = "VER",
   description = "Version of Visual Studio to build release with"
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "commit",
   value       = "SPEC",
   description = "Git commit/tag to build Clink release from"
}
