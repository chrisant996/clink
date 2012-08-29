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
local function get_current_git_branch()
    for line in io.popen("git branch 2>nul"):lines() do
        local m = line:match("%* (.+)$")
        if m then
            return m
        end
    end

    return nil
end

--------------------------------------------------------------------------------
local function get_last_git_commit()
    for line in io.popen("git log -1 --format=oneline 2>nul"):lines() do
		return line:sub(1, 6)
    end

    return "?"
end

--------------------------------------------------------------------------------
local to = ".build/"..(_ACTION or "nullaction")
local clink_ver = _OPTIONS["clink_ver"] or get_current_git_branch() or "HEAD"

--------------------------------------------------------------------------------
local pchheader_original = pchheader
local function pchheader_fixed(header)
    if _ACTION == "vs2010" then
        header = path.getname(header)
    end

    pchheader_original(header)
end
pchheader = pchheader_fixed

--------------------------------------------------------------------------------
if _ACTION and _ACTION ~= "clean" and _ACTION:find("clink_") == nil then
    -- Create a shim premake4 script so we can call premake from sln folder.
    if not shimmed then
        os.mkdir(to)
        local out = io.open(to.."/premake4.lua", "w")
        if out then
            out:write("shimmed = 1", "\n")
            out:write("_WORKING_DIR = \"".._WORKING_DIR.."\"")
            out:write("dofile(\"".._SCRIPT.."\")", "\n")
            io.close(out)
        end
    end
end

--------------------------------------------------------------------------------
local function build_postbuild(src, cfg)
    src = path.getabsolute(src)
    src = path.translate(src)

    local dest = to.."/bin/"..cfg
    dest = path.getabsolute(dest)
    dest = path.translate(dest)
    postbuildcommands("copy /y \""..src.."\" \""..dest.."\" 1>nul 2>nul")
end

--------------------------------------------------------------------------------
local function setup_cfg(cfg)
    configuration(cfg)
        targetdir(to.."/bin/"..cfg)
        objdir(to.."/obj/"..cfg)

    configuration({cfg, "x32"})
        targetsuffix("_x86")
        defines("PLATFORM=x86")

    configuration({cfg, "x64"})
        targetsuffix("_x64")
        defines("PLATFORM=x64")
end

--------------------------------------------------------------------------------
solution("clink")
    configurations({"debug", "release"})
    platforms({"x32", "x64"})
    location(to)

    flags("Symbols")
    defines("HAVE_CONFIG_H")
    defines("HANDLE_MULTIBYTE")
    defines("CLINK_VERSION=AS_STR("..clink_ver..")")
    defines("CLINK_COMMIT=AS_STR("..get_last_git_commit()..")")
    defines("STATIC_GETOPT")
    includedirs("readline/compat")
    includedirs("readline")

    setup_cfg("release")
    setup_cfg("debug")

    configuration("release")
        flags("OptimizeSize")

    configuration("vs*")
        defines("_CRT_SECURE_NO_WARNINGS")
        defines("_CRT_NONSTDC_NO_WARNINGS")

--------------------------------------------------------------------------------
project("readline")
    language("c")
    kind("staticlib")

    files("readline/readline/*.c")
    files("readline/readline/*.h")
    files("readline/compat/*.c")
    files("readline/compat/*.h")

    excludes("readline/readline/emacs_keymap.c")
    excludes("readline/readline/vi_keymap.c")

--------------------------------------------------------------------------------
project("getopt")
    language("c")
    kind("staticlib")
    files("getopt/*")

--------------------------------------------------------------------------------
project("lua")
    language("c")
    kind("staticlib")

    files("lua/src/*.c")
    files("lua/src/*.h")
    excludes("lua/src/lua.c")
    excludes("lua/src/luac.c")

--------------------------------------------------------------------------------
project("clink_dll")
    language("c")
    kind("sharedlib")
    links("lua")
    links("readline")
    links("clink_shared")
    includedirs("lua/src")
    includedirs("clink")
    defines("CLINK_DLL_BUILD")
    defines("CLINK_USE_READLINE")
    defines("CLINK_USE_LUA")
    pchsource("clink/dll/clink_pch.c")
    pchheader("clink/dll/clink_pch.h")
    files("clink/dll/*")

    configuration("release")
        build_postbuild("clink/dll/clink_inputrc", "release")
        build_postbuild("clink/dll/clink*.lua", "release")

    configuration("debug")
        build_postbuild("clink/dll/clink_inputrc", "debug")
        build_postbuild("clink/dll/clink*.lua", "debug")

    configuration("vs*")
        links("dbghelp")

--------------------------------------------------------------------------------
project("clink_loader")
    language("c")
    kind("consoleapp")
    links("clink_shared")
    links("getopt")
    targetname("clink")
    includedirs("clink")
    includedirs("getopt")
    files("clink/loader/*")
    pchsource("clink/loader/clink_pch.c")
    pchheader("clink/loader/clink_pch.h")

    configuration("release")
        build_postbuild("CHANGES", "release")
        build_postbuild("LICENSE", "release")
        build_postbuild("clink/loader/clink.bat", "release")

    configuration("debug")
        build_postbuild("clink/loader/clink.bat", "debug")

--------------------------------------------------------------------------------
project("clink_shared")
    language("c")
    kind("staticlib")
    pchsource("clink/shared/clink_pch.c")
    pchheader("clink/shared/clink_pch.h")
    files("clink/shared/*")

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_release",
    description = "Prepares a release of clink.",
    execute = function ()
        -- Helper funciton to show executed commands to TTY
        local exec = function(cmd)
            print("----------------------------------------------")
            print("-- EXEC: "..cmd)
            print("--\n")
            os.execute(cmd)
        end

        local git_checkout = clink_ver
        clink_ver = clink_ver:upper()

        -- Build the output directory name
        local target_dir = ".build\\release\\"
        target_dir = target_dir..os.date("%Y%m%d_%H%M%S")
        target_dir = target_dir.."_"
        target_dir = target_dir..clink_ver

        target_dir = path.translate(path.getabsolute(target_dir)).."\\"

        if not os.isdir(target_dir..".") then
            exec("md "..target_dir)
        end

        -- If we're not building DEV, create a clone and checkout correct version
        -- and build it.
        local src_dir_name = "."
        if clink_ver ~= "DEV" then
            src_dir_name = "clink_"..clink_ver.."_src"
            local code_dir = target_dir..src_dir_name
            if not os.isdir(code_dir..".") then
                exec("md "..code_dir)
            end

            -- clone repro in release folder and checkout the specified version
            exec("git clone . "..code_dir)
            if not os.chdir(code_dir) then
                print("Failed to chdir to '"..code_dir.."'")
                return
            end
            exec("git checkout "..git_checkout)
        end

        -- Build the code.
        exec("premake4 --clink_ver="..clink_ver.." vs2010")
        exec("msbuild /m /v:q /p:configuration=release /p:platform=win32 .build/vs2010/clink.sln")
        exec("msbuild /m /v:q /p:configuration=release /p:platform=x64 .build/vs2010/clink.sln")

        local src = ".build\\vs2010\\bin\\release\\"
        local dest = target_dir.."clink_"..clink_ver

        -- Do a coarse check to make sure there's a build available.
        if not os.isdir(src..".") then
            print("There's no build available in '"..src.."'")
            return
        end

        -- Copy release files to a directory.
        local manifest = {
            "clink_x*.exe",
            "clink_dll_x*.dll",
            "clink_dll_x*.pdb",
            "clink_inputrc",
            "clink*.lua",
            "CHANGES",
            "LICENSE",
            "clink.bat",
        }

        exec("md "..dest)
        for _, mask in ipairs(manifest) do
            exec("copy "..src..mask.." "..dest)
        end

        -- Lump lua files together.
        exec("move "..dest.."\\clink.lua "..dest.."\\clink._lua")

        local lua_lump = io.open(dest.."\\clink._lua", "a")
        for _, i in ipairs(os.matchfiles(dest.."/*.lua")) do
            i = path.translate(i)
            print("lumping "..i)

            lua_lump:write("\n--------------------------------------------------------------------------------\n")
            lua_lump:write("-- ", path.getname(i), "\n")
            lua_lump:write("--\n\n")
            for l in io.lines(i) do lua_lump:write(l, "\n") end
        end
        lua_lump:close()

        exec("del /q "..dest.."\\*.lua")
        exec("move "..dest.."\\clink._lua "..dest.."\\clink.lua")

        -- Build the installer.
        exec("makensis /DCLINK_SOURCE="..dest.." /DCLINK_VERSION="..clink_ver.." clink.nsi")

        -- Tidy up code directory.
        if clink_ver ~= "DEV" then
            exec("rd /q /s .build")
            exec("rd /q /s .git")
            exec("del /q .gitignore")

            os.chdir(target_dir)
            exec("7z a -r "..target_dir.."clink_"..clink_ver.."_src.zip "..src_dir_name)
            exec("rd /q /s "..src_dir_name)
        end

        -- Move PDBs out of the way, package release up as zips
        os.chdir(dest)
        exec("move *.pdb ..")
        exec("7z a -r ../clink_"..clink_ver..".zip ../clink_"..clink_ver)
        exec("7z a -r ../clink_"..clink_ver.."_pdb.zip ../*.pdb")
        exec("del /q ..\\*.pdb")
    end
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "clink_ver",
   value       = "VER",
   description = "The version of clink to build when using the clink_release action"
}

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_cmd_proj",
    description = "Adds a project to a vs2010 solution for running cmd.exe",
    execute = function ()
        local sln_dir = _WORKING_DIR.."/.build/vs2010"
        local out = io.open(sln_dir.."/clink.sln", "a")
        if out then
            out:write("\n")
            out:write("Project(\"{911E67C6-3D85-4FCE-B560-20A9C3E3FF48}\") = \"cmd\", \""..os.getenv("windir").."\\system32\\cmd.exe\", \"{DF6212E5-A3BE-4EA8-B4AA-BAD0ADA67955}\"\n")
            out:write("ProjectSection(DebuggerProjectSystem) = preProject\n")
            out:write("Executable = "..os.getenv("windir").."\\system32\\cmd.exe\n")
            out:write("StartingDirectory = "..path.translate(sln_dir).."\\bin\\debug\n")
            out:write("Arguments = /k clink inject --scripts ".._WORKING_DIR.."/clink/dll\n")
            out:write("EndProjectSection\n")
            out:write("EndProject\n")
            io.close(out)
        end
    end
}
