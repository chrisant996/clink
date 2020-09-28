-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local to = ".build/"..(_ACTION or "nullaction")



--------------------------------------------------------------------------------
local function get_git_info()
    local git_cmd = "git branch --verbose --no-color 2>nul"
    for line in io.popen(git_cmd):lines() do
        local _, _, name, commit = line:find("^%*.+%s+([^ )]+)%)%s+([a-f0-9]+)")
        if name and commit then
            return name, commit:sub(1, 6)
        end

        local _, _, name, commit = line:find("^%*%s+([^ ]+)%s+([a-f0-9]+)")
        if name and commit then
            return name, commit:sub(1, 6)
        end
    end

    return "NAME?", "COMMIT?"
end

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
        defines("CLINK_"..cfg:upper())
        targetdir(to.."/bin/"..cfg)
        objdir(to.."/obj/")

    configuration({cfg, "x32"})
        targetsuffix("_x86")

    configuration({cfg, "x64"})
        targetsuffix("_x64")
end



--------------------------------------------------------------------------------
local function clink_project(name)
    project(name)
    flags("fatalwarnings")
    language("c++")
end

--------------------------------------------------------------------------------
local function clink_lib(name)
    clink_project(name)
    kind("staticlib")
end

--------------------------------------------------------------------------------
local function clink_dll(name)
    clink_project(name)
    kind("sharedlib")
end

--------------------------------------------------------------------------------
local function clink_exe(name)
    clink_project(name)
    kind("consoleapp")
end



--------------------------------------------------------------------------------
clink_git_name, clink_git_commit = get_git_info()

workspace("clink")
    configurations({"debug", "release", "final"})
    platforms({"x32", "x64"})
    location(to)

    characterset("MBCS")
    flags("NoManifest")
    staticruntime("on")
    rtti("off")
    symbols("on")
    exceptionhandling("off")
    defines("HAVE_CONFIG_H")
    defines("HANDLE_MULTIBYTE")
    defines("CLINK_COMMIT="..clink_git_commit)

    setup_cfg("final")
    setup_cfg("release")
    setup_cfg("debug")

    configuration("debug")
        optimize("off")

    configuration("final")
        optimize("full")
        omitframepointer("on")
        flags("NoBufferSecurityCheck")

    configuration({"final", "vs*"})
        flags("LinkTimeOptimization")

    configuration("release")
        optimize("full")

    configuration("debug or release")
        defines("CLINK_BUILD_ROOT=\""..path.getabsolute(to).."\"")

    configuration("vs*")
        defines("_HAS_EXCEPTIONS=0")
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
    excludes("readline/readline/support/wcwidth.c")

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
project("luac")
    language("c")
    kind("consoleapp")
    links("lua")
    files("lua/src/luac.c")

--------------------------------------------------------------------------------
clink_lib("clink_lib")
    includedirs("clink/lib/include/lib")
    includedirs("clink/core/include")
    includedirs("clink/terminal/include")
    includedirs("readline")
    includedirs("readline/compat")
    files("clink/lib/src/**")
    files("clink/lib/include/**")

    includedirs("clink/lib/src")
    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/lib/src/pch.cpp")

--------------------------------------------------------------------------------
clink_lib("clink_lua")
    includedirs("clink/lua/include/lua")
    includedirs("clink/core/include")
    includedirs("clink/lib/include")
    includedirs("clink/process/include")
    includedirs("lua/src")
    files("clink/lua/src/**")
    files("clink/lua/include/**")
    files("clink/lua/scripts/**")

    includedirs("clink/lua/src")
    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/lua/src/pch.cpp")

--------------------------------------------------------------------------------
clink_lib("clink_core")
    includedirs("clink/core/include/core")
    files("clink/core/src/**")
    files("clink/core/include/**")

    includedirs("clink/core/src")
    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/core/src/pch.cpp")

--------------------------------------------------------------------------------
clink_lib("clink_terminal")
    includedirs("clink/terminal/include/terminal")
    includedirs("clink/core/include")
    files("clink/terminal/src/**")
    files("clink/terminal/include/**")

    includedirs("clink/terminal/src")
    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/terminal/src/pch.cpp")

--------------------------------------------------------------------------------
clink_lib("clink_process")
    includedirs("clink/core/include")
    includedirs("clink/process/include/process")
    files("clink/process/src/**")
    files("clink/process/include/**")

    includedirs("clink/process/src")
    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/process/src/pch.cpp")
        inlining("auto") -- required by the inject lambda in process::remote_call
        editAndContinue("off") -- required by the inject lambda in process::remote_call
        omitframepointer("off") -- required by the inject lambda in process::remote_call
        -- <SupportJustMyCode>false</SupportJustMyCode> -- required by the inject lambda in process::remote_call

--------------------------------------------------------------------------------
clink_lib("clink_app_common")
    includedirs("clink/app/src")
    includedirs("clink/core/include")
    includedirs("clink/lib/include")
    includedirs("clink/lua/include")
    includedirs("clink/process/include")
    includedirs("clink/terminal/include")
    includedirs("getopt")
    includedirs("lua/src")
    includedirs("readline")
    includedirs("readline/compat")
    files("clink/app/src/**")
    files("clink/app/scripts/**")
    excludes("clink/app/src/dll/main.cpp")
    excludes("clink/app/src/loader/main.cpp")

    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/app/src/pch.cpp")

--------------------------------------------------------------------------------
clink_dll("clink_app_dll")
    targetname("clink_dll")
    links("clink_app_common")
    links("clink_core")
    links("clink_lib")
    links("clink_lua")
    links("clink_process")
    links("clink_terminal")
    links("getopt")
    links("lua")
    links("readline")
    links("version")
    files("clink/app/src/dll/main.cpp")
    files("clink/app/src/version.rc")

    configuration("vs*")
        links("dbghelp")

--------------------------------------------------------------------------------
clink_exe("clink_app_exe")
    flags("OmitDefaultLibrary")
    targetname("clink")
    links("clink_app_dll")
    files("clink/app/src/loader/main.cpp")
    files("clink/app/src/version.rc")

    configuration("final")
        postbuild_copy("CHANGES", "final")
        postbuild_copy("LICENSE", "final")
        postbuild_copy("clink/app/src/loader/clink.bat", "final")

    configuration("release")
        postbuild_copy("clink/app/src/loader/clink.bat", "release")

    configuration("debug")
        postbuild_copy("clink/app/src/loader/clink.bat", "debug")

--------------------------------------------------------------------------------
clink_exe("clink_test")
    exceptionhandling("on")
    links("clink_app_common")
    links("clink_core")
    links("clink_lib")
    links("clink_lua")
    links("clink_process")
    links("clink_terminal")
    links("lua")
    links("readline")
    includedirs("clink/test/src")
    includedirs("clink/app/src")
    includedirs("clink/core/include")
    includedirs("clink/lib/include")
    includedirs("clink/lib/include/lib")
    includedirs("clink/lib/src")
    includedirs("clink/lua/include")
    includedirs("clink/terminal/include")
    includedirs("lua/src")
    files("clink/app/test/*.cpp")
    files("clink/core/test/*.cpp")
    files("clink/lua/test/*.cpp")
    files("clink/lib/test/*.cpp")
    files("clink/terminal/test/*.cpp")
    files("clink/test/**")

    configuration("vs*")
        pchheader("pch.h")
        pchsource("clink/test/src/pch.cpp")

--------------------------------------------------------------------------------
dofile("docs/premake5.lua")
dofile("installer/premake5.lua")
dofile("embed.lua")
