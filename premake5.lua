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
function get_current_git_branch()
    for line in io.popen("git branch --no-color 2>nul"):lines() do
        local m = line:match("%* (.+)$")
        if m then
            return m
        end
    end

    return nil
end

--------------------------------------------------------------------------------
function get_last_git_commit()
    local git_cmd = "git log -1 --format=oneline --no-color 2>nul"
    for line in io.popen(git_cmd):lines() do
        return line:sub(1, 6)
    end

    return "?"
end

--------------------------------------------------------------------------------
function useXpToolset(base, cfg)
    local p = "v110_xp"
    if _ACTION > "vs2012" then
        p = "v120_xp"
    end 

    if _ACTION > "vs2010" then
        _p(2,'<PlatformToolset>%s</PlatformToolset>', p)
    end
end
premake.override(premake.vstudio.vc2010, 'platformToolset', useXpToolset)

--------------------------------------------------------------------------------
-- Work around a bug in Premake5
path.normalize = function(i) return i end

--------------------------------------------------------------------------------
clink_ver = _OPTIONS["clink_ver"] or "DEV"
local to = ".build/"..(_ACTION or "nullaction")

--------------------------------------------------------------------------------
local keys = { "clink_ver_major", "clink_ver_minor", "clink_ver_point" }

-- Divide up the version number into major, minor and point parts.
for i in clink_ver:gmatch("%d+") do
    print(keys[1])
    _G[keys[1]] = i
    table.remove(keys, 1)
end

-- Reset remaining keys to 0
for _, i in ipairs(keys) do
    _G[i] = "0"
end

-- 47000 = magic offset. 'stamp' gives us an hourly counter that shouldn't wrap
-- for around 7-8 years.
clink_ver_stamp = (math.floor(os.time() / 3600) - 47000) % 0x10000

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

    characterset("MBCS")
    flags("Symbols")
    flags("StaticRuntime")
    defines("HAVE_CONFIG_H")
    defines("HANDLE_MULTIBYTE")
    defines("CLINK_VERSION=AS_STR("..clink_ver..")")
    defines("CLINK_COMMIT=AS_STR("..get_last_git_commit()..")")
    defines("CLINK_VER_MAJOR="..clink_ver_major)
    defines("CLINK_VER_MINOR="..clink_ver_minor)
    defines("CLINK_VER_POINT="..clink_ver_point)
    defines("CLINK_VER_STAMP="..clink_ver_stamp)
    defines("STATIC_GETOPT")
    includedirs("readline/compat")
    includedirs("readline")

    setup_cfg("release")
    setup_cfg("debug")

    configuration("release")
        optimize("size")

    configuration("vs*")
        defines("_CRT_SECURE_NO_WARNINGS")
        defines("_CRT_NONSTDC_NO_WARNINGS")

    configuration("gmake")
        defines("__MSVCRT_VERSION__=0x0601")
        defines("WINVER=0x0502")

--------------------------------------------------------------------------------
project("readline")
    language("c")
    kind("staticlib")

    defines("BUILD_READLINE")
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
project("clink_lib")
    language("c++")
    kind("staticlib")
    includedirs("clink")
    includedirs("clink/lib")
    files("clink/lib/**")

    configuration("vs*")
        pchsource("clink/lib/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_dll")
    language("c")
    kind("sharedlib")
    links("lua")
    links("readline")
    links("clink_lib")
    links("clink_shared")
    includedirs("lua/src")
    includedirs("clink")
    includedirs("clink/lib")
    files("clink/dll/*")
    files("clink/version.rc")

    configuration("release")
        build_postbuild("clink/lib/rl/clink_inputrc_base", "release")
        build_postbuild("clink/lua/*.lua", "release")

    configuration("debug")
        build_postbuild("clink/lib/rl/clink_inputrc_base", "debug")
        build_postbuild("clink/lua/*.lua", "debug")

    configuration("vs*")
        links("dbghelp")
        pchsource("clink/dll/pch.c")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_loader")
    language("c")
    kind("consoleapp")
    links("clink_shared")
    links("getopt")
    links("version")
    targetname("clink")
    includedirs("clink")
    includedirs("getopt")
    includedirs("lua/src")
    files("clink/dll/settings.c")
    files("clink/loader/*")
    files("clink/version.rc")

    configuration("release")
        build_postbuild("CHANGES", "release")
        build_postbuild("LICENSE", "release")
        build_postbuild("clink/loader/clink.bat", "release")

    configuration("debug")
        build_postbuild("clink/loader/clink.bat", "debug")

    configuration("vs*")
        pchsource("clink/loader/pch.c")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_shared")
    language("c")
    kind("staticlib")
    files("clink/shared/*")

    configuration("vs*")
        pchsource("clink/shared/pch.c")
        pchheader("pch.h")

--------------------------------------------------------------------------------
--[[
project("clink_test")
    language("c")
    kind("consoleapp")
    links("lua")
    links("getopt")
    links("readline")
    links("clink_lib")
    links("clink_shared")
    defines("GETWCH_IMPL=getwch_automatic")
    includedirs("getopt")
    includedirs("lua/src")
    includedirs("clink/dll")
    includedirs("clink")
    files("clink/test/*")

    configuration("vs*")
        pchsource("clink/lib/pch.c")
        pchheader("pch.h")
--]]

--------------------------------------------------------------------------------
newoption {
   trigger     = "clink_ver",
   value       = "VER",
   description = "The version of clink to build or being built."
}

--------------------------------------------------------------------------------
dofile("docs/premake5.lua")
dofile("installer/premake5.lua")

-- vim: expandtab
