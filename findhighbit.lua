-- This Lua script recursively searches the current directory for source code
-- files that contain bytes >= 0x80 without including a UTF8 byte order mark.
--
-- Run this via:
--      lua52.exe findhighbit.lua

local bad = 0
local total = 0

local function scan(name)
    local f = io.open(name, "rb")
    if not f then
        return
    end

    total = total + 1

    local n = 0
    for line in f:lines() do
        n = n + 1
        if n == 1 and line:find("^\xef\xbb\xbf") then
            break
        end
        local pos = line:find("[\x80-\xff]")
        if pos then
            print(name.." -- \x1b[91mline "..n..", pos "..pos.."\x1b[m")
            bad = bad + 1
            break
        end
    end

    f:close()
end

local function is_file(name)
    local f = io.open(name, "rb")
    if f then
        f:close()
        return true
    end
end

local function can_scan(name)
    return name:sub(1, 1) ~= "." and name ~= "examples"
end

local function traverse(dir)
    local f = io.popen(string.format('2>nul dir /b "%s"', dir))
    if not f then
        return
    end

    for line in f:lines() do
        if can_scan(line) then
            line = line:lower()
            local name = dir.."/"..line
            if not is_file(name) then
                traverse(name)
            elseif name:find("%.[hc]$") or name:find("%.cpp$") then
                scan(name)
            end
        end
    end

    f:close()
end

traverse(".")

if bad > 0 then
    print()
end

print(total.." file(s) scanned.")

if bad > 0 then
    print("\x1b[91m"..bad.." file(s) missing UTF8 BOM.\x1b[m")
else
    print("\x1b[92mAll files ok.\x1b[m")
end
