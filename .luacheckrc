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
        "_co_error_handler",
        "_compat_warning",
        "_error_handler",
        "_error_handler_ret",
        "clink",
        "CLINK_EXE",
        "console",
        "coroutine.override_isgenerator",
        "coroutine.override_isprompt",
        "coroutine.override_src",
        "io",
        "log",
        "NONL",
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
