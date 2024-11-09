--------------------------------------------------------------------------------
local function exec(str)
    -- Premake5 bug (see docs/premake5.lua)
    local x = path.normalize
    path.normalize = function (y) return y end
    os.execute(str)
    path.normalize = x
end

--------------------------------------------------------------------------------
local function spairs(t, order)
    -- collect the keys
    local keys = {}
    for k in pairs(t) do keys[#keys+1] = k end

    -- if order function given, sort by it by passing the table and keys a, b,
    -- otherwise just sort the keys
    if order then
        table.sort(keys, function(a,b) return order(t, a, b) end)
    else
        table.sort(keys)
    end

    -- return the iterator function
    local i = 0
    return function()
        i = i + 1
        if keys[i] then
            return keys[i], t[keys[i]]
        end
    end
end

--------------------------------------------------------------------------------
local function compare_values(t, a, b)
    return t[a] < t[b]
end

--------------------------------------------------------------------------------
local function do_embed(debug_info)
    -- Find the Lua compilers.
    local archs = {
        ["64"] = { luac = os.matchfiles(".build/*/bin/final/luac_x64.exe")[1] },
        ["86"] = { luac = os.matchfiles(".build/*/bin/final/luac_x86.exe")[1] },
    }

    if debug_info == nil then
        debug_info = true -- Include debug info by default.
    end

    local function strip(file)
        return (debug_info or path.getname(file) == "error.lua") and "" or " -s"
    end

    for name, arch in spairs(archs) do
        if not arch.luac then
            error("Unable to find Lua compiler binary (x"..name.." final).")
        end

        arch.luac = arch.luac:gsub("/", "\\")
    end

    local manifests = os.matchfiles("clink/**/_manifest.lua")
    for _, manifest in spairs(manifests, compare_values) do
        local root = path.getdirectory(manifest)

        out = path.join(root, "lua_scripts.cpp")
        print("\n"..out)
        out = io.open(out, "w")
        out:write("#include \"pch.h\"\n")
        out:write("#include <core/embedded_scripts.h>\n")
        out:write("#if defined(CLINK_USE_EMBEDDED_SCRIPTS)\n")

        -- Write each sanitised script to 'out' as a global variable.
        local symbols = {}
        local manifest = dofile(manifest)
        for _, file in spairs(manifest.files, compare_values) do
            local name = path.getname(file)
            local symbol = manifest.name .. "_" .. name:gsub("%.", "_") .. "_script"
            table.insert(symbols, symbol)

            file = path.join(root, file)
            print("   "..file)

            for archname, arch in spairs(archs) do
                out:write("#if ARCHITECTURE == "..archname.."\n")

                -- Compile the input Lua script to binary.
                local dbgname = "@~clink~/"..manifest.name.."/"..name -- To enable detecting "built in" scripts.
                exec(arch.luac..strip(file).." -R "..dbgname.." -o .build/embed_temp "..file)
                local bin_in = io.open(".build/embed_temp", "rb")
                local bin_data = bin_in:read("*a")
                bin_in:close()
                os.remove(".build/embed_temp")

                local crlf_counter = 0
                out:write("const uint8 " .. symbol .. "_[] = {\n")
                for byte in string.gmatch(bin_data, ".") do
                    out:write(string.format("0x%02x, ", byte:byte()))
                    crlf_counter = crlf_counter + 1
                    if crlf_counter > 16 then
                        out:write("\n")
                        crlf_counter = 0
                    end
                end
                out:write("};\n")
                out:write("extern const uint8* const "..symbol.." = "..symbol.."_;\n")
                out:write("extern const int32 "..symbol.."_len = sizeof("..symbol.."_);\n")

                out:write("#endif // ARCHITECTURE == "..archname.."\n")

                print("      x"..archname.." : "..tostring(#bin_data).." bytes")
            end
        end

        -- Some debug stuff so loose files can be loaded in debug builds.
        symbols = {}
        out:write("#else\n")
        for _, file in spairs(manifest.files, compare_values) do
            local symbol = path.getname(file):gsub("%.", "_")
            symbol = manifest.name .. "_" .. symbol .. "_file"
            table.insert(symbols, symbol)

            file = file:gsub("\\", "/")
            out:write("extern const char* const " .. symbol .. " = CLINK_BUILD_ROOT \"/../../" .. root .. "/" .. file .. "\";\n")
        end

        out:write("#endif\n")
        out:close()
    end
end

--------------------------------------------------------------------------------
local function escape_cpp(text)
    return text:gsub("([?\"\\])", "\\%1")
end

--------------------------------------------------------------------------------
local function write_case(out, line, count, note)
    local s = "\"" .. escape_cpp(line) .. "\",  "
    local pad = 56 - #s
    if pad > 0 then
        s = s .. string.rep(" ", pad)
    end
    note = note and ("  **" .. note .. "**") or ""
    out:write(s .. "// case #" .. count .. note .. "\n")
end

--------------------------------------------------------------------------------
local function do_wildmatch()
    local expected = 219
    local count = 0

    local out = "wildmatch/tests/t3070-wildmatch.i"
    local special1 = [==[match 1 1 '\' '[\\]']==]
    local special2 = [==[match 1 1 '\' '[\\,]']==]
    local special3 = [==[match 1 1 '\' '[[-\]]']==]

    print("\n"..out)

    local file = io.open("wildmatch/tests/t3070-wildmatch.sh", "r")
    out = io.open(out, "w")

    local header = {
        "// Generated from t3070-wildmatch.sh by 'premake5 embed'.",
        "",
        "// Test case format:",
        "//",
        "//  <x>match <wmode> <fnmode> <string> <pattern>",
        "//  <x>imatch <wmode> <string> <pattern>",
        "//  <x>pathmatch <wmode> <string> <pattern>",
        "//",
        "// match        Tests with wildmatch() and fnmatch(), and with slashes and backslashes.",
        "// imatch       Tests with wildmatch() ignoring case, with slashes and backslashes.",
        "// pathmatch    Tests with wildmatch() without WM_PATHNAME, with slashes and backslashes.",
        "//",
        "// <x>          / to run the test only with the verbatim <string>,",
        "//              \\ to run the test only with slashes in <string> converted to backslashes,",
        "//              or leave off <x> to run the test once each way.",
        "//",
        "// <wmode>      1 if the test is expected to match with wildmatch(),",
        "//              0 if the test is expected to fail,",
        "//              or any other value to skip running the test with wildmatch().",
        "//",
        "// <fnmode>     1 if the test is expected to match with fnmatch(),",
        "//              0 if the test is expected to fail,",
        "//              or any other value to skip running the test with fnmatch().",
        "",
        "static const char* const c_cases[] = {",
        "",
    }

    for _,line in ipairs(header) do
        out:write(line)
        out:write("\n")
    end

    local keep_blank
    for line in file:lines() do
        if line:find("^#") then
            local comment = line:match("^#([^!].+)$")
            if comment then
                keep_blank = true
                out:write("//" .. comment .. "\n")
            end
        elseif keep_blank and line == "" then
            out:write("\n")
        else
            local op = line:match("^(%w+) ")
            if op then
                local note
                if line == special1 or line == special2 or line == special3 then
                    note = "MODIFIED"
                    line = "/" .. line
                elseif line == "match 1 0 'deep/foo/bar/baz/x' 'deep/**/***/****/*****'" then
                    note = "MODIFIED"
                    line = "match 1 0 'deep/foo/bar/baz/x' 'deep/**/***/****'"
                end
                count = count + 1
                write_case(out, line, count, note)
            end

            if line == "match 0 0 'foo/bar' 'foo[/]bar'" then
                count = count + 1
                write_case(out, "match 0 0 'foo/bar' 'foo[^a-z]bar'", count, "ADDITIONAL")
            elseif line == "pathmatch 1 foo/bar 'foo[/]bar'" then
                count = count + 1
                write_case(out, "pathmatch 1 foo/bar 'foo[^a-z]bar'", count, "ADDITIONAL")
            elseif line == "match 1 0 'deep/foo/bar/baz/x' 'deep/**/***/****'" then
                count = count + 1
                write_case(out, "match 1 1 'deep/foo/bar/baz/x' 'deep/**/***/****/*****'", count, "ADDITIONAL")
            end
        end
    end

    -- Don't need to force an extra blank line at the end, because
    -- t3070-wildmatch.sh itself includes an extra blank line at the end.
    out:write("};\n")
    out:write("\n")
    out:write("static const int c_expected_count = " .. expected .. ";\n")

    file:close()
    out:close()

    print("   " .. count .. " test cases")

    if count ~= expected then
        error("\x1b[0;31;1mFAILED: expected " .. expected .. " tests; found " .. count .. " instead.")
    end
end

--------------------------------------------------------------------------------
local function load_indexed_emoji_table(file)
    -- Collect the emoji characters.
    --
    -- This uses a simplistic approach of taking the first codepoint from each
    -- line in the input file.
    local indexed = {}
    for line in file:lines() do
        local x = line:match("^([0-9A-Fa-f]+) ")
        if x then
            local d = tonumber(x, 16)
            if d then
                indexed[d] = true
            end
        end
    end
    return indexed
end

--------------------------------------------------------------------------------
local function output_character_ranges(out, tag, indexed, filtered)
    -- Declaration.
    out:write("\nstatic const struct interval " .. tag .. "[] = {\n\n")

    -- Build sorted array of characters.
    local chars = {}
    for d, _ in pairs(indexed) do
        if not (filtered and filtered[d]) then
            table.insert(chars, d)
        end
    end
    table.sort(chars)

    -- Optimize the set of characters into ranges.
    local count_ranges = 0
    local first
    local last
    for _, d in ipairs(chars) do
        if last and last + 1 ~= d then
            count_ranges = count_ranges + 1
            out:write(string.format("{ 0x%X, 0x%X },\n", first, last))
            first = nil
        end
        if not first then
            first = d
        end
        last = d
    end
    if first then
        count_ranges = count_ranges + 1
        out:write(string.format("{ 0x%X, 0x%X },\n", first, last))
    end

    out:write("\n};\n")

    return chars, count_ranges
end

--------------------------------------------------------------------------------
local function do_emojis()
    local out = "clink/terminal/src/emoji-test.i"

    print("\n"..out)

    local file = io.open("clink/terminal/src/emoji-test.txt", "r")
    local filter = io.open("clink/terminal/src/emoji-filter.txt", "r")
    local fe0f = io.open("clink/terminal/src/emoji-fe0f.txt", "r")
    local mono = io.open("clink/terminal/src/emoji-mono.txt", "r")
    out = io.open(out, "w")

    local header = {
        "// Generated from emoji-test.txt by 'premake5 embed'.",
    }

    for _,line in ipairs(header) do
        out:write(line)
        out:write("\n")
    end

    -- Collect the emoji characters.
    local indexed = load_indexed_emoji_table(file)
    local filtered = load_indexed_emoji_table(filter)
    local possible_unqualified_half_width = load_indexed_emoji_table(fe0f)
    local mono_emojis = load_indexed_emoji_table(mono)
    file:close()
    filter:close()
    fe0f:close()

    -- Output ranges of double-width emoji characters.
    local emojis, count_ranges = output_character_ranges(out, "emojis", indexed, filtered)

    -- Output ranges of double-width monochrome emoji characters.
    output_character_ranges(out, "mono_emojis", mono_emojis)

    -- Output ranges of emoji characters which may be half-width if unqualified.
    local half_width = output_character_ranges(out, "possible_unqualified_half_width", possible_unqualified_half_width, nil)

    out:close()

    print("   " .. #emojis .. " emojis; " .. count_ranges .. " ranges")
    print("   " .. #half_width .. " possible unqualified half width emojis")
end

--------------------------------------------------------------------------------
newaction {
    trigger = "embed",
    description = "Clink: Update embedded scripts for Clink",
    execute = function ()
        do_embed()
        do_wildmatch()
        do_emojis()
    end
}

--------------------------------------------------------------------------------
newaction {
    trigger = "embed_debug",
    description = "Clink: Update embedded scripts for Clink with debugging info",
    execute = function ()
        do_embed(true--[[debug_info]])
        do_wildmatch()
        do_emojis()
    end
}

--------------------------------------------------------------------------------
newaction {
    trigger = "embed_nodebug",
    description = "Clink: Update embedded scripts for Clink without debugging info",
    execute = function ()
        do_embed(false--[[debug_info]])
        do_wildmatch()
        do_emojis()
    end
}
