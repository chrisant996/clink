-- This Lua script recursively searches the current directory for source code
-- files that contain bytes >= 0x80 without including a UTF8 byte order mark.
--
-- Run this via:
--      lua52.exe findhighbit.lua
--      lua52.exe findhighbit.lua --fix

local bad = 0
local fixed = 0
local total = 0

local noflags
local fix
for _, name in ipairs(arg) do
    if not noflags and name:find('^%-%-') then
        if name == '--' then
            noflags = true
        elseif name:lower() == '--fix' then
            fix = true
        else
            error('Unrecognized flag \''..name..'\'.')
        end
    else
        error('Too many arguments.')
    end
end

local bom = "\xef\xbb\xbf"
local bom_pattern = "^"..bom
local highbit_pattern = "[\x80-\xff]"

local function scan(name, dolines)
    local f = io.open(name, "rb")
    if not f then
        return
    end

    total = total + 1

    local missing
    if not dolines then
        -- This "*a" optimization saves around 5-10% elapsed time.
        local content = f:read("*a")
        f:close()
        if not content:find(bom_pattern) and content:find(highbit_pattern) then
            total = total - 1
            if scan(name, true) and fix then
                f = io.open(name, "wb")
                if not f then
                    print("\x1b[91mError trying to fix missing BOM.\x1b[m")
                else
                    f:write(bom)
                    f:write(content)
                    f:close()
                    print("\x1b[92mFixed.\x1b[m")
                    fixed = fixed + 1
                end
            end
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
                missing = true
                break
            end
        end
        f:close()
    end

    return missing
end

local function can_scan(name)
    return not name:find("\\%.[^\\]*\\") and
        not name:find("\\examples\\") and
        not name:find("\\readline\\readline\\")
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
        print("\x1b["..(fix and "93" or "91").."m"..bad.." file(s) missing UTF8 BOM.\x1b[m")
        if not fix then
            print("\x1b[91mRun again with --fix to add missing BOMs.\x1b[m")
        end
        if bad > fixed then
            print("\x1b[91mOne or more files were not fixed.\x1b[m")
        else
            print("\x1b[92mAll files fixed.\x1b[m")
        end
    else
        print("\x1b[92mAll files ok.\x1b[m")
    end
end

analyze()
