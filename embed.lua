--------------------------------------------------------------------------------
local function exec(str)
    -- Premake5 bug (see docs/premake5.lua)
    local x = path.normalize
    path.normalize = function (y) return y end
    os.execute(str)
    path.normalize = x
end

--------------------------------------------------------------------------------
local function do_embed()
    local luac = os.matchfiles(".build/*/bin/final/luac_x64.exe")[1]
    if not luac then
        error("Unable to find Lua compiler binary (final).")
    end

    luac = luac:gsub("/", "\\")

    local manifests = os.matchfiles("clink/**/_manifest.lua")
    for _, manifest in ipairs(manifests) do
        local root = path.getdirectory(manifest)

        out = path.join(root, "lua_scripts.cpp")
        print("-- " .. out)
        out = io.open(out, "w")
        out:write("#include \"pch.h\"\n")
        out:write("#if defined(CLINK_FINAL)\n")

        -- Write each sanitised script to 'out' as a global variable.
        local symbols = {}
        local manifest = dofile(manifest)
        for _, file in ipairs(manifest.files) do
            local name = path.getname(file)
            local symbol = manifest.name .. "_" .. name:gsub("%.", "_") .. "_script"
            table.insert(symbols, symbol)

            file = path.join(root, file)

            -- Compile the input Lua script to binary.
            exec(luac.." -s -o .build/embed_temp "..file)
            local bin_in = io.open(".build/embed_temp", "rb")
            local bin_data = bin_in:read("*a")
            bin_in:close()
            os.remove(".build/embed_temp")

            print("     " .. file .. "  -->  " .. symbol)
            local crlf_counter = 0
            out:write("const unsigned char " .. symbol .. "_[] = {\n")
            for byte in string.gmatch(bin_data, ".") do
                out:write(string.format("0x%02x, ", byte:byte()))
                crlf_counter = crlf_counter + 1
                if crlf_counter > 16 then
                    out:write("\n")
                    crlf_counter = 0
                end
            end
            out:write("};\n")
            out:write("unsigned char const* "..symbol.." = "..symbol.."_;\n")
            out:write("int "..symbol.."_len = sizeof("..symbol.."_);\n")
        end

        -- Some debug stuff so loose files can be loaded in debug builds.
        symbols = {}
        out:write("#else\n")
        for _, file in ipairs(manifest.files) do
            local symbol = path.getname(file):gsub("%.", "_")
            symbol = manifest.name .. "_" .. symbol .. "_file"
            table.insert(symbols, symbol)

            file = file:gsub("\\", "/")
            out:write("const char* " .. symbol .. " = CLINK_BUILD_ROOT \"/../../" .. root .. "/" .. file .. "\";\n")
        end

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
