return {
    exclude_files = {
        ".install",
        ".lua",
        ".luarocks",
        "modules/JSON.lua",
        "lua_modules"
    },
    files = {
        spec = { std = "+busted" },
    },
    globals = {
        "clink",
        "console",
        "io",
        "log",
        "os",
        "path",
        "pause",
        "rl",
        "rl_state",
        "settings",
        "string",
        "unicode",
    }
}
