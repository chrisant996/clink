-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

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
function use_xp_toolset(base, cfg)
    local p = "v110_xp"
    if _ACTION > "vs2012" then
        p = "v120_xp"
    end

    if _ACTION > "vs2010" then
        _p(2,'<PlatformToolset>%s</PlatformToolset>', p)
    end
end
premake.override(premake.vstudio.vc2010, 'platformToolset', use_xp_toolset)

--------------------------------------------------------------------------------
-- Work around a bug in Premake5
path.normalize = function(i) return i end

--------------------------------------------------------------------------------
clink_ver = _OPTIONS["clink_ver"] or "DEV"
local to = ".build/"..(_ACTION or "nullaction")

local to_root = path.getdirectory(to)
if not os.isdir(to_root) then
    os.mkdir(to_root)
end

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
local function postbuild_copy(src, cfg)
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
workspace("clink")
    configurations({"debug", "release", "final"})
    platforms({"x32", "x64"})
    location(to)

    characterset("MBCS")
    flags("NoManifest")
    flags("StaticRuntime")
    flags("Symbols")
    rtti("off")
    exceptionhandling("off")
    defines("HAVE_CONFIG_H")
    defines("HANDLE_MULTIBYTE")
    defines("CLINK_VERSION=AS_STR("..clink_ver..")")
    defines("CLINK_COMMIT=AS_STR("..get_last_git_commit()..")")
    defines("CLINK_VER_MAJOR="..clink_ver_major)
    defines("CLINK_VER_MINOR="..clink_ver_minor)
    defines("CLINK_VER_POINT="..clink_ver_point)
    defines("CLINK_VER_STAMP="..clink_ver_stamp)
    defines("STATIC_GETOPT")
    defines("_HAS_EXCEPTIONS=0")

    setup_cfg("final")
    setup_cfg("release")
    setup_cfg("debug")

    configuration("debug")
        optimize("off")

    configuration("final")
        optimize("full")
        flags("NoFramePointer")
        flags("NoBufferSecurityCheck")
        flags("LinkTimeOptimization")
        defines("CLINK_EMBED_LUA_SCRIPTS")

    configuration("release")
        optimize("full")

    configuration("vs*")
        buildoptions("/FC")
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
    includedirs("readline")
    includedirs("readline/compat")
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
project("catch")
    language("c++")
    kind("staticlib")
    exceptionhandling("on")
    files("catch/*.cpp")
    files("catch/*.hpp")

--------------------------------------------------------------------------------
project("clink_lib")
    language("c++")
    kind("staticlib")
    includedirs("clink")
    includedirs("clink/lib") -- MODE4
    includedirs("clink/lib/include") -- MODE4
    includedirs("clink/lib/include/lib")
    includedirs("clink/core/include")
    includedirs("clink/terminal/include")
    includedirs("lua/src")
    files("clink/lib/**")
    excludes("clink/lib/lua/**")

    configuration("vs*")
        pchsource("clink/lib/src/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_core")
    language("c++")
    kind("staticlib")
    includedirs("clink/core/include/core")
    files("clink/core/src/**")

    configuration("vs*")
        pchsource("clink/core/src/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_terminal")
    language("c++")
    kind("staticlib")
    includedirs("clink/terminal/include/terminal")
    includedirs("clink/core/include")
    files("clink/terminal/src/**")

    configuration("vs*")
        pchsource("clink/terminal/src/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_process")
    language("c++")
    kind("staticlib")
    includedirs("clink/core/include")
    includedirs("clink/process/include/process")
    files("clink/process/src/**")

    configuration("vs*")
        pchsource("clink/process/src/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_base")
    language("c++")
    kind("staticlib")
    includedirs("clink/app")
    includedirs("clink/lib/include")
    includedirs("clink/core/include")
    includedirs("clink/process/include")
    includedirs("clink/terminal/include")
    includedirs("getopt")
    includedirs("lua/src")
    includedirs("readline")
    includedirs("readline/compat")
    files("clink/app/**")
    excludes("clink/app/dll/main.cpp")
    excludes("clink/app/loader/main.cpp")

    configuration("vs*")
        pchsource("clink/app/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
project("clink_dll")
    language("c++")
    kind("sharedlib")
    targetname("clink")
    links("clink_base")
    links("clink_core")
    links("clink_lib")
    links("clink_process")
    links("clink_terminal")
    links("getopt")
    links("lua")
    links("readline")
    links("version")
    files("clink/app/dll/main.cpp")
    files("clink/app/version.rc")

    configuration("vs*")
        links("dbghelp")

--------------------------------------------------------------------------------
project("clink_loader")
    language("c++")
    kind("consoleapp")
    flags("OmitDefaultLibrary")
    removeflags("Symbols")
    targetname("clink")
    links("clink_dll")
    files("clink/app/loader/main.cpp")
    files("clink/app/version.rc")

    configuration("final")
        postbuild_copy("CHANGES", "final")
        postbuild_copy("LICENSE", "final")
        postbuild_copy("clink/app/loader/clink.bat", "final")

    configuration("release")
        postbuild_copy("clink/app/loader/clink.bat", "release")

    configuration("debug")
        postbuild_copy("clink/app/loader/clink.bat", "debug")

--------------------------------------------------------------------------------
project("clink_test")
    language("c++")
    kind("consoleapp")
    exceptionhandling("on")
    links("catch")
    links("clink_base")
    links("clink_core")
    links("clink_lib")
    links("clink_terminal")
    links("lua")
    links("readline")
    includedirs("catch")
    includedirs("clink/lib/include")
    includedirs("clink/app")
    includedirs("clink/core/include")
    includedirs("clink/terminal/include")
    includedirs("lua/src")
    files("clink/core/test/*.cpp")
    files("clink/terminal/test/*.cpp")
    files("clink/test/*.cpp")
    files("clink/test/*.h")

    configuration("vs*")
        pchsource("clink/test/pch.cpp")
        pchheader("pch.h")

--------------------------------------------------------------------------------
newoption {
   trigger     = "clink_ver",
   value       = "VER",
   description = "The version of clink to build or being built."
}

--------------------------------------------------------------------------------
dofile("docs/premake5.lua")
dofile("installer/premake5.lua")
dofile("embed.lua")
