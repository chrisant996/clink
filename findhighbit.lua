-- This Lua script recursively searches the current directory for source code
-- files that contain bytes >= 0x80 without including a UTF8 byte order mark.
--
-- Run this via:
--      lua52.exe findhighbit.lua

local bad = 0
local total = 0

local bom_pattern = "^\xef\xbb\xbf"
local highbit_pattern = "[\x80-\xff]"

local function scan(name, dolines)
    local f = io.open(name, "rb")
    if not f then
        return
    end

    total = total + 1

    if not dolines then
        -- This "*a" optimization saves around 5-10% elapsed time.
        local content = f:read("*a")
        if not content:find(bom_pattern) and content:find(highbit_pattern) then
            total = total - 1
            scan(name, true)
        end
    else
        local n = 0
        for line in f:lines() do
            n = n + 1
            if n == 1 and line:find(bom_pattern) then
                break
            end
            local pos = line:find(highbit_pattern)
            if pos then
                print(name.." -- \x1b[91mline "..n..", pos "..pos.."\x1b[m")
                bad = bad + 1
                break
            end
        end
    end

    f:close()
end

local function can_scan(name)
    return not name:find("\\%.[^\\]*\\") and not name:find("\\examples\\")
end

local function traverse(dir)
    local f = io.popen(string.format('2>nul dir /b /s /a:-d "%s"', dir))
    if not f then
        return
    end

    for name in f:lines() do
        if can_scan(name) then
            name = name:lower()
            if name:find("%.[hc]$") or name:find("%.cpp$") then
                scan(name)
            end
        end
    end

    f:close()
end

local function analyze()
    bad = 0
    total = 0

    local start = os.clock()
    traverse(".")
    local elapsed = os.clock() - start

    if bad > 0 then
        print()
    end

    print(string.format("%u file(s) scanned in %.2f sec.", total, elapsed))

    if bad > 0 then
        print("\x1b[91m"..bad.." file(s) missing UTF8 BOM.\x1b[m")
    else
        print("\x1b[92mAll files ok.\x1b[m")
    end
end

analyze()
