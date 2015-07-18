-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function generate_file(source_path, out)
    print("  << " .. source_path)
    for line in io.open(source_path, "r"):lines() do
        local include = line:match("%$%(INCLUDE +([^)]+)%)")
        if include then
            include = path.getdirectory(source_path) .. "/" .. include
            generate_file(include, out)
        else
            line = line:gsub("%$%(CLINK_VERSION%)", tostring(clink_ver))
            out:write(line .. "\n")
        end
    end
end

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_docs",
    description = "Generates Clink's documentation.",
    execute = function ()
        out_path = ".build/docs/clink.html"

        os.execute("1>nul 2>nul md .build\\docs")

        print("")
        print(">> " .. out_path)
        generate_file("docs/clink.html", io.open(out_path, "w"))
        print("")
    end
}
