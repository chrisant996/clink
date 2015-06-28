--------------------------------------------------------------------------------
local function load_script(script_path)
    -- Taken from https://github.com/premake/premake-core

    script_path = path.getabsolute(script_path)
    local f = io.open(script_path)
    local s = f:read("*a")
    f:close()

    -- strip tabs
    s = s:gsub("[\t]", "")

    -- strip any CRs
    s = s:gsub("[\r]", "")

    -- strip out block comments
    s = s:gsub("[^\"']%-%-%[%[.-%]%]", "")
    s = s:gsub("[^\"']%-%-%[=%[.-%]=%]", "")
    s = s:gsub("[^\"']%-%-%[==%[.-%]==%]", "")

    -- strip out inline comments
    s = s:gsub("\n%-%-[^\n]*", "\n")

    -- escape backslashes
    s = s:gsub("\\", "\\\\")

    -- strip duplicate line feeds
    s = s:gsub("\n+", "\n")

    -- strip out leading comments
    s = s:gsub("^%-%-[^\n]*\n", "")

    -- escape double quote marks
    s = s:gsub("\"", "\\\"")

    -- escape line feeds
    s = s:gsub("\n", "\\n\"\n\"")

    return "\"" .. s .. "\""
end

--------------------------------------------------------------------------------
local function do_embed()
    local manifests = os.matchfiles("clink/**/_manifest.lua")
    for _, manifest in ipairs(manifests) do
        local root = path.getdirectory(manifest)

        out = path.join(root, "lua_scripts.cpp")
        print("-- " .. out)
        out = io.open(out, "w")
        out:write("#include \"pch.h\"\n")
        out:write("#ifdef CLINK_EMBED_LUA_SCRIPTS\n")

        -- Write each sanitised script to 'out' as a global variable.
        local symbols = {}
        local manifest = dofile(manifest)
        for _, file in ipairs(manifest.files) do
            local name = path.getname(file)
            local symbol = manifest.name .. "_" .. name:gsub("%.", "_") .. "_script"
            table.insert(symbols, symbol)

            file = path.join(root, file)

            print("     " .. file .. "  -->  " .. symbol)
            out:write("const char* " .. symbol .. " = \n")
            out:write(load_script(file) .. ";")
        end

        -- Write a manifest variable of all embedded scripts in the .cpp file.
        out:write("const char* " .. manifest.name .. "_lua_scripts[] = {")
        for _, symbol in ipairs(symbols) do
            out:write(symbol .. ",")
        end
        out:write("nullptr,};\n")

        -- Some debug stuff so loose can files can be loaded in debug builds.
        symbols = {}
        out:write("#else\n")
        out:write("const char* " .. manifest.name .. "_embed_path = __FILE__;\n")
        for _, file in ipairs(manifest.files) do
            local symbol = path.getname(file):gsub("%.", "_")
            symbol = manifest.name .. "_" .. symbol .. "_file"
            table.insert(symbols, symbol)

            file = file:gsub("\\", "/")
            out:write("const char* " .. symbol .. " = \"" .. file .. "\";\n")
        end

        -- Write a manifest variable of all embedded scripts in the .cpp file.
        out:write("const char* " .. manifest.name .. "_lua_files[] = {")
        for _, symbol in ipairs(symbols) do
            out:write(symbol .. ",")
        end
        out:write("nullptr,};\n")

        out:write("#endif\n")
        out:close()
    end
end

--------------------------------------------------------------------------------
newaction {
    trigger = "embed",
    description = ".",
    execute = function ()
        do_embed()
    end
}
