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
local ver = "DEV"

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
    includedirs("readline/compat")
    includedirs("readline")

    setup_cfg("release")
    setup_cfg("debug")

    configuration("release")
        flags("OptimizeSize")

    configuration("gmake")
        defines("CLINK_VERSION=\\\""..ver.."\\\"")
        defines("_WIN32_WINNT=0x500")

    configuration("vs*")
        defines("CLINK_VERSION=\""..ver.."\"")
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
    files("clink_util.c")
    files("clink_pch.c")
    files("clink.h")
    files("clink_*.lua")
    includedirs("lua/src")
    defines("CLINK_DLL_BUILD")
    defines("CLINK_USE_READLINE")
    defines("CLINK_USE_LUA")
    pchheader("clink_pch.h")
    pchsource("clink_pch.c")

    configuration("release")
        build_postbuild("clink_inputrc", "release")
        build_postbuild("clink_*.lua", "release")
        build_postbuild("clink.bat", "release")

    configuration("debug")
        build_postbuild("clink_inputrc", "debug")
        build_postbuild("clink_*.lua", "debug")
        build_postbuild("clink.bat", "debug")

    configuration("vs*")
        links("dbghelp")

--------------------------------------------------------------------------------
project("clink_sandbox")
    language("c")
    kind("consoleapp")
    links("clink_dll")
    files("clink_sandbox.c")

--------------------------------------------------------------------------------
project("clink")
    language("c")
    kind("consoleapp")
    files("clink_loader.c")
    files("clink_util.c")

--------------------------------------------------------------------------------
newaction {
    trigger = "release",
    description = "Prepares a release of clink.",
    execute = function ()
        os.execute("premake4 vs2010")
        os.execute("msbuild /v:m /p:configuration=release /p:platform=win32 /t:rebuild .build/vs2010/clink.sln")
        os.execute("msbuild /v:m /p:configuration=release /p:platform=x64 /t:rebuild .build/vs2010/clink.sln")

        local src = ".build\\vs2010\\bin\\release\\"
        local dest = ".build\\release\\"..os.date("%Y%m%d_%H%M%S").."_"..ver.."\\clink_"..ver
        if not os.isdir(src..".") then
            return
        end

        local manifest = {
            "clink_x*.exe",
            "clink_dll_x*.dll",
            "clink_dll_x*.pdb",
            "clink_inputrc",
            "clink_*.lua",
            "..\\..\\..\\..\\clink.bat",
        }

        os.execute("md "..dest)
        for _, mask in ipairs(manifest) do
            os.execute("copy "..src..mask.." "..dest)
        end

        os.execute("cd "..dest.." && move *.pdb ..")
        os.execute("cd "..dest.."\\.. && 7z a -r clink_"..ver..".zip clink_"..ver)
        os.execute("makensis /DCLINK_SOURCE="..dest.." clink.nsi")
    end
}
