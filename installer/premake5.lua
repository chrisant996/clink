-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local any_warnings_or_failures = nil

--------------------------------------------------------------------------------
local release_manifest = {
    "clink.bat",
    "clink.lua",
    "clink_x*.exe",
    "clink*.dll",
    "clink*.ico",
    "CHANGES",
    "LICENSE",
    "clink_x*.pdb",
    "clink_dll_x*.pdb",
    "_default_settings",
    "_default_inputrc",
}

--------------------------------------------------------------------------------
local function warn(msg)
    print("\x1b[0;33;1mWARNING: " .. msg.."\x1b[m")
    any_warnings_or_failures = true
end

--------------------------------------------------------------------------------
local function failed(msg)
    print("\x1b[0;31;1mFAILED: " .. msg.."\x1b[m")
    any_warnings_or_failures = true
end

--------------------------------------------------------------------------------
local exec_lead = "\n"
local function exec(cmd, silent)
    print(exec_lead .. "## EXEC: " .. cmd)

    if silent then
        cmd = "1>nul 2>nul "..cmd
    else
        -- cmd = "1>nul "..cmd
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
local function exec_with_retry(cmd, tries, delay, silent)
    while tries > 0 do
        if exec(cmd, silent) then
            return true
        end

        tries = tries - 1

        if tries > 0 then
            print("... waiting to retry ...")
            local target = os.clock() + delay
            while os.clock() < target do
                -- Busy wait, but this is such a rare case that it's not worth
                -- trying to be more efficient.
            end
        end
    end

    return false
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
local function rename(src, dest)
    src = path.translate(src)
    return exec("ren " .. src .. " " .. dest, true)
end

--------------------------------------------------------------------------------
local function file_exists(name)
    local f = io.open(name, "r")
    if f ~= nil then
        io.close(f)
        return true
    end
    return false
end

--------------------------------------------------------------------------------
local function have_required_tool(name, fallback)
    if exec("where " .. name, true) then
        return name
    end

    if fallback then
        local t
        if type(fallback) == "table" then
            t = fallback
        else
            t = { fallback }
        end
        for _,dir in ipairs(t) do
            local file = dir.."\\"..name..".exe"
            if file_exists(file) then
                return '"'..file..'"'
            end
        end
    end

    return nil
end

--------------------------------------------------------------------------------
newaction {
    trigger = "nsis",
    description = "Clink: Creates a pre-release installer for Clink (reltype is debug by default)",
    execute = function()
        local premake = _PREMAKE_COMMAND
        local root_dir = path.getabsolute(".build/vs2019/bin").."/"
        local code_dir = path.getabsolute(".").."/"
        local config = _OPTIONS["config"] or "debug"

        exec_lead = ""

        if not os.isdir(code_dir.."clink") or not os.isdir(code_dir.."readline") or not os.isdir(code_dir.."lua") then
            error(code_dir.." does not appear to be a Clink repo root directory.");
        end

        -- Check we have the tools we need.
        local have_msbuild = have_required_tool("msbuild", { "c:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\MSBuild\\Current\\Bin", "c:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Enterprise\\MSBuild\\Current\\Bin" })
        local have_nsis = have_required_tool("makensis", "c:\\Program Files (x86)\\NSIS")
        if not have_msbuild then error("MSBUILD NOT FOUND.") end
        if not have_nsis then error("NSIS NOT FOUND.") end

        -- Build the code.
        local x86_ok = true
        local x64_ok = true
        local toolchain = "ERROR"
        local build_code = function (target)
            target = target or "build"

            toolchain = _OPTIONS["vsver"] or "vs2019"
            os.chdir(".build/" .. toolchain)

            x86_ok = exec(have_msbuild .. " /m /v:q /p:configuration=" .. config .. " /p:platform=win32 clink.sln /t:" .. target)
            x64_ok = exec(have_msbuild .. " /m /v:q /p:configuration=" .. config .. " /p:platform=x64 clink.sln /t:" .. target)

            os.chdir("../..")
        end

        build_code()

        local src = path.getabsolute(".build/" .. toolchain .. "/bin/" .. config) .. "/"

        -- Do a coarse check to make sure there's a build available.
        if not os.isdir(src .. ".") or not (x86_ok or x64_ok) then
            error("There's no build available in '" .. src .. "'")
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
        local docversion = version:match("%d+%.%d+%.%d+")

        -- Create the output directory.
        local dest = path.getabsolute(".build/nsis").."/"
        rmdir(dest)
        mkdir(dest)

        -- Copy release files to a directory.
        for _, mask in ipairs(release_manifest) do
            local from = src
            if mask == "CHANGES" or mask == "LICENSE" or mask == "_default_settings" or mask == "_default_inputrc" then
                from = code_dir
            elseif mask == "clink*.ico" then
                from = code_dir.."clink/app/resources/"
            elseif mask:sub(-4) == ".pdb" then
                from = nil
            end
            if from then
                copy(from .. mask, dest)
            end
        end

        -- Generate documentation.
        print()
        exec(premake .. " docs --docver="..docversion)
        copy(".build/docs/clink.html", dest)

        -- Build the installer.
        local nsis_ok = false
        if have_nsis then
            local nsis_cmd = have_nsis
            nsis_cmd = nsis_cmd .. " /DCLINK_BUILD=" .. dest
            nsis_cmd = nsis_cmd .. " /DCLINK_VERSION=" .. version
            nsis_cmd = nsis_cmd .. " /DCLINK_SOURCE=" .. code_dir
            nsis_cmd = nsis_cmd .. " " .. code_dir .. "/installer/clink.nsi"
            nsis_ok = exec(nsis_cmd)
            if nsis_ok then
                rename(dest.."_setup.exe", "clink_setup_" .. config .. ".exe")
            end
        end

        -- Report some facts about what just happened.
        print("\n\n")
        if not nsis_ok then     warn("INSTALLER PACKAGE FAILED") end
        if not x86_ok then      failed("x86 BUILD FAILED") end
        if not x64_ok then      failed("x64 BUILD FAILED") end
    end
}

--------------------------------------------------------------------------------
newaction {
    trigger = "release",
    description = "Clink: Creates a release of Clink",
    execute = function ()
        local premake = _PREMAKE_COMMAND
        local root_dir = path.getabsolute(".build/release").."/"

        -- Check we have the tools we need.
        local have_msbuild = have_required_tool("msbuild", { "c:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\MSBuild\\Current\\Bin", "c:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Enterprise\\MSBuild\\Current\\Bin" })
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

                x86_ok = exec(have_msbuild .. " /m /v:q /p:configuration=final /p:platform=win32 clink.sln /t:" .. target)
                x64_ok = exec(have_msbuild .. " /m /v:q /p:configuration=final /p:platform=x64 clink.sln /t:" .. target)

                os.chdir("../..")
            elseif have_mingw then
                target = target or "build"

                toolchain = "gmake"
                exec(premake .. " " .. toolchain)
                os.chdir(".build/" .. toolchain)

                x86_ok = exec(have_mingw .. " CC=gcc config=final_x32 -j%number_of_processors% " .. target)
                x64_ok = exec(have_mingw .. " CC=gcc config=final_x64 -j%number_of_processors% " .. target)

                os.chdir("../..")
            else
                error("Unable to locate either msbuild.exe or mingw32-make.exe")
            end
        end

        -- Build everything.
        build_code("luac")
        exec(premake .. " " .. (_OPTIONS["dbginfo"] and "embed_debug" or "embed"))
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
        local docversion = version:match("%d+%.%d+%.%d+")

        -- Now we know the version we can create our output directory.
        local target_dir = root_dir..os.date("%Y%m%d_%H%M%S").."_"..version.."/"
        rmdir(target_dir)
        mkdir(target_dir)

        local clink_suffix = "clink."..version
        local dest = target_dir..clink_suffix.."/"
        mkdir(dest)

        -- Copy release files to a directory.
        for _, mask in ipairs(release_manifest) do
            local from = src
            if mask == "_default_settings" or mask == "_default_inputrc" then
                from = code_dir
            elseif mask:match("clink.*%.ico") then
                from = code_dir.."clink/app/resources/"
            end
            copy(from .. mask, dest)
        end

        -- Generate documentation.
        exec(premake .. " docs --docver="..docversion)
        copy(".build/docs/clink.html", dest)

        -- Build the installer.
        local nsis_ok = false
        if have_nsis then
            local nsis_cmd = have_nsis
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
        if not exec_with_retry("move ~working " .. src_dir_name, 3, 10.0) then
            error("Failed to move ~working to " .. src_dir_name)
        end

        if have_7z then
            os.chdir(src_dir_name)
            exec(have_7z .. " a -r  " .. target_dir..clink_suffix .. "_src.zip .")
        end
        os.chdir(target_dir)
        rmdir(src_dir_name)

        -- Package the release and the pdbs separately.
        os.chdir(dest)
        if have_7z then
            if have_msbuild then
                exec(have_7z .. " a -r  ../"..clink_suffix .. "_symbols.zip  *.pdb")
            end
            exec(have_7z .. " a -x!*.pdb -r  ../"..clink_suffix .. ".zip  *")
        end

        -- Report some facts about what just happened.
        print("\n\n")
        if not have_7z then     warn("7-ZIP NOT FOUND -- Packing to .zip files was skipped.") end
        if not have_nsis then   warn("NSIS NOT FOUND -- No installer was created.") end
        if not nsis_ok then     warn("INSTALLER PACKAGE FAILED") end
        if not x86_ok then      failed("x86 BUILD FAILED") end
        if not x64_ok then      failed("x64 BUILD FAILED") end
        if not any_warnings_or_failures then
            print("\x1b[0;32;1mRelease "..version.." built successfully.\x1b[m")
        end
    end
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "vsver",
   value       = "VER",
   description = "Clink: Version of Visual Studio to build release with"
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "docver",
   value       = "DOCVER",
   description = "Clink: Clink version to inject in documentation"
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "commit",
   value       = "SPEC",
   description = "Clink: Git commit/tag to build Clink release from"
}

--------------------------------------------------------------------------------
newoption {
    trigger     = "config",
    value       = "CONFIG",
    description = "Clink: The build configuration for 'nsis' command",
    allowed     = {
        { "debug",      "For local use; debug mode code, uses local Lua scripts"},
        { "release",    "For local use; optimized code, uses local Lua scripts" },
        { "final",      "Can share installer externally; optimized code, Lua scripts are embedded" },
    }
}

--------------------------------------------------------------------------------
newoption {
    trigger     = "dbginfo",
    description = "Clink: Include debug info in embedded Lua scripts"
}
