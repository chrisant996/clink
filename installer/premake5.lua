-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local any_warnings_or_failures = nil
local include_arm64 = true
local msbuild_locations = {
    "c:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin",
    "c:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\MSBuild\\Current\\Bin",
    "c:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Enterprise\\MSBuild\\Current\\Bin",
}

--------------------------------------------------------------------------------
local release_manifest = {
    "clink.bat",
    "clink.lua",
    "clink_x*.exe",
    "clink_dll_x*.dll",
    "clink*.ico",
    "CHANGES",
    "LICENSE",
    "clink_x*.pdb",
    "clink_dll_x*.pdb",
    "_default_settings",
    "_default_inputrc",
    "Dracula.clinktheme",
    "Enhanced Defaults.clinktheme",
    "Plain.clinktheme",
    "Solarized Dark.clinktheme",
    "Solarized Light.clinktheme",
    "Tomorrow.clinktheme",
    "Tomorrow Night.clinktheme",
    "Tomorrow Night Blue.clinktheme",
    "Tomorrow Night Bright.clinktheme",
    "Tomorrow Night Eighties.clinktheme",
    "agnoster.clinkprompt",
    "Antares.clinkprompt",
    "bureau.clinkprompt",
    "darkblood.clinkprompt",
    "Headline.clinkprompt",
    "jonathan.clinkprompt",
    "oh-my-posh.clinkprompt",
    "pure.clinkprompt",
}

if include_arm64 then
    table.insert(release_manifest, "clink_arm64.exe")
    table.insert(release_manifest, "clink_dll_arm64.dll")
end

--------------------------------------------------------------------------------
-- Some timestamp services for code signing:
--  http://timestamp.digicert.com
--  http://time.certum.pl
--  http://sha256timestamp.ws.symantec.com/sha256/timestamp
--  http://timestamp.comodoca.com/authenticode
--  http://timestamp.comodoca.com
--  http://timestamp.sectigo.com
--  http://timestamp.globalsign.com
--  http://tsa.starfieldtech.com
--  http://timestamp.entrust.net/TSS/RFC3161sha2TS
--  http://tsa.swisssign.net
local cert_name = "Open Source Developer, Christopher Antos"
--local timestamp_service = "http://timestamp.digicert.com"
local timestamp_service = "http://time.certum.pl"
local sign_command = string.format(' sign /n "%s" /fd sha256 /td sha256 /tr %s ', cert_name, timestamp_service)
local verify_command = string.format(' verify /pa ')

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
local function print_reverse(msg, prolog)
    prolog = prolog or "\n"
    print(prolog .. "\x1b[0;7m " .. msg .. " \x1b[m")
end

--------------------------------------------------------------------------------
local function path_normalize(pathname, quote)
    local pre = ""
    pathname = pathname:gsub("\"", "")
    pathname = pathname:gsub("/", "\\")
    local pre = pathname:match("^\\") or ""
    pathname = pathname:sub(#pre + 1)
    pathname = pathname:gsub("\\+", "\\")
    pathname = pre .. pathname
    if quote then
        if pathname:find("\\$") then
            pathname = pathname .. "\\"
        end
        pathname = '"' .. pathname .. '"'
    end
    return pathname
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

    dir = path_normalize(dir, true)
    local ret = exec("md " .. dir, true)
    if not ret then
        error("Failed to create directory '" .. dir .. "' ("..tostring(ret)..")", 2)
    end
end

--------------------------------------------------------------------------------
local function rmdir(dir)
    if not os.isdir(dir) then
        return
    end

    dir = path_normalize(dir, true)
    return exec("rd /q /s " .. dir, true)
end

--------------------------------------------------------------------------------
local function unlink(file)
    file = path_normalize(file, true)
    return exec("del /q " .. file, true)
end

--------------------------------------------------------------------------------
local function copy(src, dest)
    src = path_normalize(src, true)
    dest = path_normalize(dest, true)
    return exec("copy /y " .. src .. " " .. dest, true)
end

--------------------------------------------------------------------------------
local function rename(src, dest)
    src = path_normalize(src, true)
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
    local vsver
    if name == "msbuild" then
        local opt_vsver = _OPTIONS["vsver"]
        if opt_vsver and opt_vsver:find("^vs") then
            vsver = opt_vsver:sub(3)
        end
    end

    if not vsver then
        if exec("where " .. name, true) then
            return name
        end
    end

    if fallback then
        local t
        if type(fallback) == "table" then
            t = fallback
        else
            t = { fallback }
        end
        for _,dir in ipairs(t) do
            if not vsver or dir:find(vsver) then
                local file = dir.."\\"..name..".exe"
                if file_exists(file) then
                    return '"'..file..'"'
                end
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
        local premake = '"'.._PREMAKE_COMMAND..'"'
        local root_dir = path.getabsolute(".build/vs2022/bin").."/"
        local code_dir = path.getabsolute(".").."/"
        local config = _OPTIONS["config"] or "debug"

        exec_lead = ""

        if not os.isdir(code_dir.."clink") or not os.isdir(code_dir.."readline") or not os.isdir(code_dir.."lua") then
            error(code_dir.." does not appear to be a Clink repo root directory.");
        end

        -- Check we have the tools we need.
        local have_msbuild = have_required_tool("msbuild", msbuild_locations)
        local have_nsis = have_required_tool("makensis", "c:\\Program Files (x86)\\NSIS")
        if not have_msbuild then error("MSBUILD NOT FOUND.") end
        if not have_nsis then error("NSIS NOT FOUND.") end

        local clink_git_name
        do
            local git_cmd = "git branch --verbose --no-color 2>nul"
            local f = io.popen(git_cmd)
            for line in f:lines() do
                local _, _, name, commit = line:find("^%*.+%s+([^ )]+)%)%s+([a-f0-9]+)%s")
                if name and commit then
                    clink_git_name = name
                    break
                end
                _, _, name, commit = line:find("^%*%s+([^ ]+)%s+([a-f0-9]+)%s")
                if name and commit then
                    clink_git_name = name
                    break
                end
            end
            f:close()
        end

        -- Build the code.
        local x86_ok = true
        local x64_ok = true
        local arm64_ok = true
        local toolchain = "ERROR"
        local build_code = function (target)
            target = target or "build"

            toolchain = _OPTIONS["vsver"] or "vs2022"
            os.chdir(".build/" .. toolchain)

            x86_ok = exec(have_msbuild .. " /m /v:q /p:configuration=" .. config .. " /p:platform=win32 clink.sln /t:" .. target)
            x64_ok = exec(have_msbuild .. " /m /v:q /p:configuration=" .. config .. " /p:platform=x64 clink.sln /t:" .. target)
            if include_arm64 then
                arm64_ok = exec(have_msbuild .. " /m /v:q /p:configuration=" .. config .. " /p:platform=arm64 clink.sln /t:" .. target)
            end

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
        local ismain = (clink_git_name == "master" or clink_git_name == "main")
        local docversion = version:match("%d+%.%d+%.%d+")
        local tagversion = ismain and docversion or version

        -- Create the output directory.
        local dest = path.getabsolute(".build/nsis").."/"
        rmdir(dest.."themes/")
        rmdir(dest)
        mkdir(dest)
        mkdir(dest.."themes/")

        -- Copy release files to a directory.
        for _, mask in ipairs(release_manifest) do
            local from = src
            local to = dest
            if mask == "CHANGES" or mask == "LICENSE" or mask == "_default_settings" or mask == "_default_inputrc" then
                from = code_dir
            elseif mask:match(".*%.clinktheme") or mask:match(".*%.clinkprompt") then
                from = code_dir.."clink/app/themes/"
                to = dest.."themes/"
            elseif mask == "clink*.ico" then
                from = code_dir.."clink/app/resources/"
            elseif mask:sub(-4) == ".pdb" then
                from = nil
            end
            if from then
                copy(from .. mask, to)
            end
        end

        -- Generate documentation.
        print()
        local doc_cmd = premake .. " docs --docver="..docversion
        if not ismain then
            doc_cmd = doc_cmd .. " --docbranch="..clink_git_name
        end
        exec(doc_cmd)
        copy(".build/docs/clink.html", dest)

        -- Build the installer.
        local nsis_ok = false
        if have_nsis then
            local nsis_cmd = have_nsis
            nsis_cmd = nsis_cmd .. " /DCLINK_BUILD=" .. dest
            nsis_cmd = nsis_cmd .. " /DCLINK_VERSION=" .. version
            nsis_cmd = nsis_cmd .. " /DCLINK_TAGVERSION=" .. tagversion
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
        if not arm64_ok then    failed("arm64 BUILD FAILED") end
    end
}

--------------------------------------------------------------------------------
newaction {
    trigger = "release",
    description = "Clink: Creates a release of Clink",
    execute = function ()
        local premake = '"'.._PREMAKE_COMMAND..'"'
        local root_dir = path.getabsolute(".build/release").."/"

        -- Check we have the tools we need.
        print_reverse("Finding tools")
        local have_msbuild = have_required_tool("msbuild", msbuild_locations)
        local have_mingw = have_required_tool("mingw32-make")
        local have_nsis = have_required_tool("makensis", "c:\\Program Files (x86)\\NSIS")
        local have_7z = have_required_tool("7z", { "c:\\Program Files\\7-Zip", "c:\\Program Files (x86)\\7-Zip" })
        local have_signtool = have_required_tool("signtool", { "c:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.22000.0\\x64" })

        -- Clone repo in release folder and checkout the specified version
        local code_dir = root_dir.."~working/"
        rmdir(code_dir)
        mkdir(code_dir)

        print_reverse("Cloning repo")
        exec("git clone . " .. code_dir)
        if not os.chdir(code_dir) then
            error("Failed to chdir to '" .. code_dir .. "'")
        end
        exec("git checkout " .. (_OPTIONS["commit"] or "HEAD"))

        local clink_git_name
        do
            local git_cmd = "git branch --verbose --no-color 2>nul"
            local f = io.popen(git_cmd)
            for line in f:lines() do
                local _, _, name, commit = line:find("^%*.+%s+([^ )]+)%)%s+([a-f0-9]+)%s")
                if name and commit then
                    clink_git_name = name
                    break
                end
                _, _, name, commit = line:find("^%*%s+([^ ]+)%s+([a-f0-9]+)%s")
                if name and commit then
                    clink_git_name = name
                    break
                end
            end
            f:close()
        end

        -- Build the code.
        local x86_ok = true
        local x64_ok = true
        local arm64_ok = true
        local toolchain = "ERROR"
        local build_code = function (target)
            if have_msbuild then
                target = target or "build"

                toolchain = _OPTIONS["vsver"] or "vs2022"
                exec(premake .. " " .. toolchain)
                os.chdir(".build/" .. toolchain)

                x86_ok = exec(have_msbuild .. " /m /v:q /p:configuration=final /p:platform=win32 clink.sln /t:" .. target)
                x64_ok = exec(have_msbuild .. " /m /v:q /p:configuration=final /p:platform=x64 clink.sln /t:" .. target)
                if include_arm64 and target ~= "luac" then
                    arm64_ok = exec(have_msbuild .. " /m /v:q /p:configuration=final /p:platform=arm64 clink.sln /t:" .. target)
                end

                os.chdir("../..")
            elseif have_mingw then
                target = target or "build"

                toolchain = "gmake"
                exec(premake .. " " .. toolchain)
                os.chdir(".build/" .. toolchain)

                x86_ok = exec(have_mingw .. " CC=gcc config=final_x32 -j%number_of_processors% " .. target)
                x64_ok = exec(have_mingw .. " CC=gcc config=final_x64 -j%number_of_processors% " .. target)
                if include_arm64 and target ~= "luac" then
                    arm64_ok = nil
                end

                os.chdir("../..")
            else
                error("Unable to locate either msbuild.exe or mingw32-make.exe")
            end
        end

        -- Build everything.
        print_reverse("Build Lua compiler")
        build_code("luac")
        print_reverse("Precompile Lua scripts")
        exec(premake .. " " .. (_OPTIONS["dbginfo"] and "embed_debug" or "embed"))
        print_reverse("Build Clink executables")
        build_code()

        local src = path.getabsolute(".build/" .. toolchain .. "/bin/final").."/"

        -- Do a coarse check to make sure there's a build available.
        if not os.isdir(src .. ".") or not (x86_ok or x64_ok) then
            error("There's no build available in '" .. src .. "'")
        end

        -- Run tests.
        if x86_ok or x64_ok then
            print_reverse("Run Clink tests")
        end
        if x86_ok then
            test_exe = path_normalize(src.."/clink_test_x86.exe")
            if not exec(test_exe) then
                error("x86 tests failed")
            end
        end
        if x64_ok then
            test_exe = path_normalize(src.."/clink_test_x64.exe")
            if not exec(test_exe) then
                error("x64 tests failed")
            end
        end

        -- Now we can sign the files.
        local sign = not _OPTIONS["nosign"]
        local signed_ok -- nil means unknown, false means failed, true means ok.
        local function sign_files(file_table)
            local orig_dir = os.getcwd()
            os.chdir(src)

            local files = ""
            for _, file in ipairs(file_table) do
                files = files .. " " .. file
            end

            -- Sectigo requests to wait 15 seconds between timestamps.
            -- Digicert and Certum don't mention any delay, so for now
            -- just let signtool do the signatures and timestamps without
            -- imposing external delays.
            signed_ok = (exec('"' .. have_signtool .. sign_command .. files .. '"') and signed_ok ~= false) or false
            -- Note: FAILS: cmd.exe /c "program" args "more args"
            --    SUCCEEDS: cmd.exe /c ""program" args "more args""

            -- Verify the signatures.
            signed_ok = (exec(have_signtool .. verify_command .. files) and signed_ok ~= false) or false

            os.chdir(orig_dir)
        end

        if sign then
            print_reverse("Sign executables")
            local sign_list = {
                "clink_x86.exe",
                "clink_dll_x86.dll",
                "clink_x64.exe",
                "clink_dll_x64.dll",
            }
            if include_arm64 then
                table.insert(sign_list, "clink_arm64.exe")
                table.insert(sign_list, "clink_dll_arm64.dll")
            end
            sign_files(sign_list)
        end

        -- Now we can extract the version from the executables.
        print_reverse("Extract version")
        local version = nil
        local clink_exe = x86_ok and "clink_x86.exe" or "clink_x64.exe"
        local ver_cmd = src:gsub("/", "\\")..clink_exe.." --version"
        for line in io.popen(ver_cmd):lines() do
            version = line
        end
        if not version then
            error("Failed to extract version from build executables")
        end
        local ismain = (clink_git_name == "master" or clink_git_name == "main")
        local docversion = version:match("%d+%.%d+%.%d+")
        local tagversion = ismain and docversion or version

        -- Now we know the version we can create our output directory.
        print_reverse("Copy release files")
        local target_dir = root_dir..os.date("%Y%m%d_%H%M%S").."_"..version.."/"
        rmdir(target_dir)
        mkdir(target_dir)

        local clink_suffix = "clink."..version
        local dest = target_dir..clink_suffix.."/"
        mkdir(dest)
        mkdir(dest.."themes/")

        -- Copy release files to a directory.
        for _, mask in ipairs(release_manifest) do
            local from = src
            local to = dest
            if mask == "_default_settings" or mask == "_default_inputrc" then
                from = code_dir
            elseif mask:match(".*%.clinktheme") or mask:match(".*%.clinkprompt") then
                from = code_dir.."clink/app/themes/"
                to = dest.."themes/"
            elseif mask:match("clink.*%.ico") then
                from = code_dir.."clink/app/resources/"
            end
            copy(from .. mask, to)
        end

        -- Generate documentation.
        print_reverse("Generate documentation")
        local doc_cmd = premake .. " docs --docver="..docversion
        if not ismain then
            doc_cmd = doc_cmd .. " --docbranch="..clink_git_name
        end
        exec(doc_cmd)
        copy(".build/docs/clink.html", dest)

        -- Build the installer.
        local nsis_ok = false
        if have_nsis then
            local nsis_cmd = have_nsis
            nsis_cmd = nsis_cmd .. " /DCLINK_BUILD=" .. path.getabsolute(dest)
            nsis_cmd = nsis_cmd .. " /DCLINK_VERSION=" .. version
            nsis_cmd = nsis_cmd .. " /DCLINK_TAGVERSION=" .. tagversion
            nsis_cmd = nsis_cmd .. " /DCLINK_SOURCE=" .. code_dir
            nsis_cmd = nsis_cmd .. " " .. code_dir .. "/installer/clink.nsi"
            print_reverse("Build setup program")
            nsis_ok = exec(nsis_cmd)
            if sign then
                print_reverse("Sign setup program")
                sign_files({path.getabsolute(dest) .. "_setup.exe"})
            end
        end

        -- Tidy up code directory.
        print_reverse("Tidy output directories")
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
            print_reverse("Zip source code")
            exec(have_7z .. " a -r  " .. target_dir..clink_suffix .. "_src.zip .")
        end
        os.chdir(target_dir)
        rmdir(src_dir_name)

        -- Package the release and the pdbs separately.
        os.chdir(dest)
        if have_7z then
            if have_msbuild then
                print_reverse("Zip symbols")
                exec(have_7z .. " a -r  ../"..clink_suffix .. "_symbols.zip  *.pdb")
            end
            print_reverse("Zip Clink files")
            exec(have_7z .. " a -x!*.pdb -r  ../"..clink_suffix .. ".zip  *")
        end

        -- Report some facts about what just happened.
        print_reverse("Status")
        print("")
        if not have_7z then     warn("7-ZIP NOT FOUND -- Packing to .zip files was skipped.") end
        if not have_nsis then   warn("NSIS NOT FOUND -- No installer was created.") end
        if not nsis_ok then     warn("INSTALLER PACKAGE FAILED") end
        if not x86_ok then      failed("x86 BUILD FAILED") end
        if not x64_ok then      failed("x64 BUILD FAILED") end
        if arm64_ok == nil then
            local x = any_warnings_or_failures
            warn("arm64 build skipped.")
            any_warnings_or_failures = x
        elseif not arm64_ok then
            failed("arm64 BUILD FAILED")
        end
        if sign and not signed_ok then
            failed("signing FAILED")
        end
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
   trigger     = "commit",
   value       = "SPEC",
   description = "Clink: Git commit/tag to build Clink release from"
}

--------------------------------------------------------------------------------
newoption {
    trigger     = "nosign",
    description = "Clink: don't sign the release files"
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
