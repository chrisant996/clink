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
local to = ".build/"..(_ACTION or "nullaction")
local clink_ver = _OPTIONS["clink_ver"] or "HEAD"

--------------------------------------------------------------------------------
local function build_postbuild(src, cfg)
    postbuildcommands("copy /y \"..\\..\\"..src.."\" \"bin\\"..cfg.."\" 1>nul 2>nul")
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
    files("clink_rl.c")
    files("clink_lua.c")
    files("clink_dll.c")
    files("clink_util.*")
    files("clink_pch.c")
    files("clink_hook.c")
    files("clink_doskey.c")
    files("clink_vm.*")
    files("clink_pe.*")
    files("clink.h")
    files("clink_*.lua")
    includedirs("lua/src")
    defines("CLINK_DLL_BUILD")
    defines("CLINK_USE_READLINE")
    defines("CLINK_USE_LUA")
    pchsource("clink_pch.c")
    pchheader("clink_pch.h")

    configuration("release")
        build_postbuild("clink_inputrc", "release")
        build_postbuild("clink_*.lua", "release")
        build_postbuild("clink_*.txt", "release")
        build_postbuild("clink.bat", "release")

    configuration("debug")
        build_postbuild("clink_inputrc", "debug")
        build_postbuild("clink_*.lua", "debug")
        build_postbuild("clink_*.txt", "debug")
        build_postbuild("clink.bat", "debug")

    configuration("vs*")
        links("dbghelp")

--------------------------------------------------------------------------------
project("clink")
    language("c")
    kind("consoleapp")
    files("clink_loader.c")
    files("clink_util.c")
    files("clink_pe.*")

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_release",
    description = "Prepares a release of clink.",
    execute = function ()
        local exec = function(cmd)
            print("----------------------------------------------")
            print("-- EXEC: "..cmd)
            print("--\n")
            os.execute(cmd)
        end

        local rls_dir_name = os.date("%Y%m%d_%H%M%S")
        local src_dir_name = "clink_"..clink_ver.."_src"
        local rls_dir = ".build\\release\\"..rls_dir_name.."_"..clink_ver.."\\"
        local code_dir = rls_dir..src_dir_name
        if not os.isdir(code_dir..".") then
            exec("md "..code_dir)
        end

        -- clone repro in release folder and checkout the specified version
        exec("git clone . "..code_dir)
        if not os.chdir(code_dir) then
            return
        end
        exec("git checkout "..clink_ver)

        -- build the code.
        exec("premake4 --clink_ver="..clink_ver.." vs2010")
        exec("msbuild /v:m /p:configuration=release /p:platform=win32 /t:rebuild .build/vs2010/clink.sln")
        exec("msbuild /v:m /p:configuration=release /p:platform=x64 /t:rebuild .build/vs2010/clink.sln")

        local src = ".build\\vs2010\\bin\\release\\"
        local dest = "..\\clink_"..clink_ver

        -- copy release files to a directory.
        local manifest = {
            "clink_x*.exe",
            "clink_dll_x*.dll",
            "clink_dll_x*.pdb",
            "clink_inputrc",
            "clink_*.lua",
            "clink_*.txt",
            "..\\..\\..\\..\\clink.bat",
        }

        exec("md "..dest)
        for _, mask in ipairs(manifest) do
            exec("copy "..src..mask.." "..dest)
        end

        -- tidy up code directory.
        exec("rd /q /s .build")
        exec("rd /q /s .git")
        exec("del /q .gitignore")

        -- move PDBs out of the way, package release up as zip and installer
        os.chdir(dest)
        exec("move *.pdb ..")
        exec("7z a -r ../clink_"..clink_ver..".zip ../clink_"..clink_ver)
        exec("7z a -r ../clink_"..clink_ver.."_src.zip ../"..src_dir_name)
        exec("7z a -r ../clink_"..clink_ver.."_pdb.zip ../*.pdb")
        exec("makensis /DCLINK_SOURCE="..dest.." ../"..src_dir_name.."/clink.nsi")

        exec("rd /q /s ..\\"..src_dir_name)
        exec("del /q ..\\*.pdb")
    end
}

--------------------------------------------------------------------------------
newoption {
   trigger     = "clink_ver",
   value       = "VER",
   description = "The version of clink to build when using the clink_release action"
}
