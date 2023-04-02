-- Copyright (c) 2020 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- Used by `clink.print()` to suppress the usual trailing newline.  The table
-- address is unique, thus `clink.print()` can test for equality.
NONL = {}

--------------------------------------------------------------------------------
-- Provide a stub implementation of pause() if the debugger isn't loaded.
local _can_pause = true
if pause == nil then
    _can_pause = false
    pause = function (message)
        if message then
            print(message)
        end
        print("can't pause; debugger not loaded.")
    end
end

--------------------------------------------------------------------------------
-- This returns the file and line for the top stack frame, starting at start.
local function _get_top_frame(start, max)
    if not start or type(start) ~= "number" or start < 1 then
        start = 1
    end
    if not max or type(max) ~= "number" or max < 1 then
        max = 26
    else
        max = max + 1
    end

    local file, line
    for f = start, max, 1 do
        local t = debug.getinfo(f, "Sl")
        if not t then
            if file and line then
                return file, line
            end
            return
        end
        file = t.short_src
        line = t.currentline
    end
end

--------------------------------------------------------------------------------
-- This returns the file and line number for the top stack frame, starting at
-- start_frame (or the next closest stack frame for which a file and line number
-- are available).
local function _get_maybe_fileline(start_frame)
    local file, line = _get_top_frame(start_frame)
    if file and line then
        return " in "..file..":"..line
    end
    return ""
end



--------------------------------------------------------------------------------
-- This is the error handler used by native code calls into Lua scripts.
function _error_handler(message)
    if _can_pause and settings.get("lua.break_on_error") then
        pause("break on error: "..message)
    end
    if settings.get("lua.traceback_on_error") then
        print(debug.traceback(message, 2))
    else
        print(message)
    end
end

--------------------------------------------------------------------------------
-- This error handler function is for use by Lua scripts making protected calls.
-- If lua.break_on_error is set it activates the debugger. It always returns the
-- error message, which is then returned by pcall().  This doesn't handle
-- lua.traceback_on_error because the caller of pcall() is already expecting an
-- error message to be returned and may print it.  I'd rather allow scripts to
-- suppress error messages than force error messages to show up twice.
function _error_handler_ret(message)
    if _can_pause and settings.get("lua.break_on_error") then
        pause("break on error: "..message)
    end
    return debug.traceback(message, 2)
end

--------------------------------------------------------------------------------
-- This is the error handler used for reporting coroutine errors.
function _co_error_handler(co, message)
    if settings.get("lua.traceback_on_error") or
            (_can_pause and settings.get("lua.break_on_error")) then
        local m = debug.traceback(co, message)
        print(m)
        log.info(m)
    else
        print(message)
        if settings.get("lua.debug") then
            log.info(debug.traceback(co, message))
        end
    end
end



--------------------------------------------------------------------------------
-- This reports a compatibility message.
local remind_how_to_disable = true
function _compat_warning(message, suffix)
    local where = _get_maybe_fileline(2) -- 2 gets the caller's file and line.
    message = message or ""
    suffix = suffix or ""

    local compat = os.getenv("CLINK_COMPAT_WARNINGS")
    compat = compat and tonumber(compat) or 1

    log.info(debug.traceback(message..suffix, 2)) -- 2 omits this function.

    if compat == 0 then
        return
    end

    if remind_how_to_disable then
        remind_how_to_disable = false
        print("Compatibility warnings will be hidden if %CLINK_COMPAT_WARNINGS% == 0.")
        print("Consider updating the Lua scripts; otherwise functionality may be impaired.")
    end
    print(message..where.." (see log file for details).")
end



--------------------------------------------------------------------------------
-- This returns true if short_src matches "{...}", which means it's an internal
-- Clink script.
function clink._is_internal_script(short_src)
    return string.match(short_src, "^{.*}$")
end



--------------------------------------------------------------------------------
--- -name:  clink.quote_split
--- -deprecated: string.explode
--- -arg:   str:string
--- -arg:   ql:string
--- -arg:   qr:string
--- -ret:   table
--- This function takes the string <span class="arg">str</span> which is quoted
--- by <span class="arg">ql</span> (the opening quote character) and
--- <span class="arg">qr</span> (the closing character) and splits it into parts
--- as per the quotes. A table of these parts is returned.
function clink.quote_split(str, ql, qr)
    if not qr then
        qr = ql
    end

    -- First parse in "pre[ql]quote_string[qr]" chunks
    local insert = table.insert
    local i = 1
    local needle = "%b"..ql..qr
    local parts = {}
    for l, r, quote in function() return str:find(needle, i) end do -- luacheck: no unused
        -- "pre"
        if l > 1 then
            insert(parts, str:sub(i, l - 1))
        end

        -- "quote_string"
        insert(parts, str:sub(l, r))
        i = r + 1
    end

    -- Second parse what remains as "pre[ql]being_quoted"
    local l = str:find(ql, i, true)
    if l then
        -- "pre"
        if l > 1 then
            insert(parts, str:sub(i, l - 1))
        end

        -- "being_quoted"
        insert(parts, str:sub(l))
    elseif i <= #str then
        -- Finally add whatever remains...
        insert(parts, str:sub(i))
    end

    return parts
end

--------------------------------------------------------------------------------
--- -name:  clink.split
--- -deprecated: string.explode
--- -arg:   str:string
--- -arg:   sep:string
--- -ret:   table
--- Splits the string <span class="arg">str</span> into pieces separated by
--- <span class="arg">sep</span>, returning a table of the pieces.
function clink.split(str, sep)
    local i = 1
    local ret = {}
    for _, j in function() return str:find(sep, i, true) end do
        table.insert(ret, str:sub(i, j - 1))
        i = j + 1
    end

    -- In Clink v0.4.9 this accidentally accesses j as a global variable, which
    -- ends up behaving as expected as long as there is no global variable j
    -- defined.  For safety, that bug is fixed here.
    --table.insert(ret, str:sub(i, j))
    table.insert(ret, str:sub(i))

    return ret
end



--------------------------------------------------------------------------------
function os.globdirs(pattern, extrainfo, flags)
    if flags == nil and type(extrainfo) == "table" then
        flags = extrainfo
        extrainfo = nil
    end

    local c, ismain = coroutine.running()
    if ismain then
        -- Use a fully native implementation for higher performance.
        return os._globdirs(pattern, extrainfo, flags)
    elseif clink._is_coroutine_canceled(c) then
        return {}
    else
        -- Yield periodically.
        local t = {}
        local g = os._makedirglobber(pattern, extrainfo, flags)
        while g:next(t) do
            coroutine.yield()
            if clink._is_coroutine_canceled(c) then
                t = {}
                break
            end
        end
        g:close()
        return t
    end
end

--------------------------------------------------------------------------------
function os.globfiles(pattern, extrainfo, flags)
    if flags == nil and type(extrainfo) == "table" then
        flags = extrainfo
        extrainfo = nil
    end

    local c, ismain = coroutine.running()
    if ismain then
        -- Use a fully native implementation for higher performance.
        return os._globfiles(pattern, extrainfo, flags)
    elseif clink._is_coroutine_canceled(c) then
        return {}
    else
        -- Yield periodically.
        local t = {}
        local g = os._makefileglobber(pattern, extrainfo, flags)
        while g:next(t) do
            coroutine.yield()
            if clink._is_coroutine_canceled(c) then
                t = {}
                break
            end
        end
        g:close()
        return t
    end
end

--------------------------------------------------------------------------------
local function normalize_stars(s)
    local start = 1
    while true do
        local sa, sz = s:find("%*%*+", start)
        if not sa then
            return s
        end

        local wildstar = true
        if sa > 1 and s:sub(sa - 1, sa - 1) ~= "/" then
            wildstar = false
        else
            local ch = s:sub(sz + 1, sz + 1)
            if ch ~= "" and ch ~= "/" then
                wildstar = false
            end
        end

        local replace = wildstar and "**" or "*"
        s = s:sub(1, sa - 1) .. replace .. s:sub(sz + 1)
        start = sa + #replace
    end
end

--------------------------------------------------------------------------------
-- DOC: When pattern ends with / it collects dirs, otherwise it collects files.
-- DOC: Flags are same as os.globfiles(), plus .period and .nocasefold.
-- DOC: Pre-order traversal.
-- TODO: unit tests...
function os.globmatch(pattern, extrainfo, flags)
    if flags == nil and type(extrainfo) == "table" then
        flags = extrainfo
        extrainfo = nil
    end
    if type(extrainfo) == "number" and extrainfo < 1 then
        extrainfo = nil
    end

    local c, ismain = coroutine.running()
    local matches = {}
    local stack = {}
    local stack_count = 0

    if not pattern then
        return {}
    end
    pattern = pattern:gsub('"', '')
    if pattern == "" or pattern == "/" then
        return {}
    end
    pattern = normalize_stars(pattern)

    local fnmatch_flags = "*"
    if flags then
        if flags.period then
            fnmatch_flags = fnmatch_flags .. "."
        end
        if flags.nocasefold then
            fnmatch_flags = fnmatch_flags .. "c"
        end
    end

    local last_yield = os.clock()
    local test_yield_bail
    if ismain then
        test_yield_bail = function ()
            if os.issignaled() then
                return true
            end
        end
    else
        test_yield_bail = function (force)
            if force or (os.clock() - last_yield > 0.001) then
                coroutine.yield()
                last_yield = os.clock()
            end
            if clink._is_coroutine_canceled(c) then
                return true
            end
        end
    end

    if pattern:sub(1, 2) == "//" then
        pattern = "//" .. pattern:sub(3):gsub("//+", "/")
    else
        pattern = pattern:gsub("//+", "/")
    end

    local do_dirs = pattern:find("/$") and true

    -- Seed initial search pattern.
    table.insert(stack, { pattern="", rest=pattern:gsub("/$", "") })
    stack_count = stack_count + 1

    -- Process the directory stack.
    while stack_count > 0 do
        if test_yield_bail() then
            return {}
        end

        -- Pop next directory from stack.
        local entry = table.remove(stack, stack_count)
        stack_count = stack_count - 1

        -- Find the longest non-wild pattern prefix, to optimize away
        -- recursion that cannot match.
        local parent = entry.pattern
        local nonwild = entry.rest:match("^[^*?[]+") or ""
        local wild = path.join(parent, nonwild.."*")
        local rest = entry.rest:sub(#nonwild +1)
        local rest_star = rest:find("**", 1, true)
        local rest_slash = rest:find("/", 1, true)
        local glob_parent = path.join(parent, nonwild:match("^(.*)/") or "")
print("POP:  pattern="..entry.pattern..",  rest="..entry.rest..",  parent="..parent..",  wild="..wild)

        -- Trim leading subdir from rest, if possible.
        local next_rest
        if rest_slash and (not rest_star or rest_slash < rest_star) then
            next_rest = rest:match("^[^/]+/(.*)$")
        else
            next_rest = rest
        end

        -- Enumerate directories and files.
        local t = {}
        local globber = os._makefileglobber(wild, extrainfo, flags)
        while globber:next(t) do
            if test_yield_bail(true) then
                globber:close()
                return {}
            end
        end
        globber:close()

        -- Process the enumerated results.
        local dirs = {}
        for _, g in ipairs(t) do
            if test_yield_bail() then
                return {}
            end

            -- Get name.
            local name = extrainfo and g.name or g
            local fullname

            -- Collect directories for recursion.
            local is_dir = name:find("\\$") and true
            if is_dir and (rest_star or rest_slash) then
                if not fullname then
                    fullname = path.join(glob_parent, name):gsub("\\", "/")
                end
print("  RECURSE:  "..fullname..",  "..next_rest)
                table.insert(dirs, { pattern=fullname, rest=next_rest })
            end

            -- Add matching directories or files.
            local leaf = not rest_slash or (rest_star and rest_star < rest_slash)
            if leaf then
                if do_dirs == is_dir then
                    if not fullname then
                        fullname = path.join(glob_parent, name):gsub("/", "\\")
                    end
                    if path.fnmatch(pattern, fullname, fnmatch_flags) then
print("    match:  "..fullname)
                        if extrainfo then
                            g.name = fullname
                            table.insert(matches, g)
                        else
                            table.insert(matches, fullname)
                        end
                    end
                end
            end
        end

        -- Add recursion directories in reverse order, to make popping simple
        -- and efficient.
        for idx = #dirs, 1, -1 do
            table.insert(stack, dirs[idx])
            stack_count = stack_count + 1
        end
    end

    return matches
end



--------------------------------------------------------------------------------
local function first_letter(s)
    -- This handles combining marks, but does not yet handle ZWJ (0x200d) such
    -- as in emoji sequences.
    local letter = ""
    for codepoint, value, combining in unicode.iter(s) do
        if value == 0x200d then
            break
        elseif not combining and #letter > 0 then
            break
        end
        letter = letter .. codepoint
    end
    return letter
end

--------------------------------------------------------------------------------
local function abbrev_child(parent, child)
    local letter = first_letter(child)
    if not letter or letter == "" then
        return child, false
    end

    local any = false
    local lcd = 0
    local dirs = os.globdirs(path.join(parent, letter .. "*"))
    for _, x in ipairs(dirs) do
        local m = string.matchlen(child, x)
        if lcd < m then
            lcd = m
        end
        any = true
    end
    lcd = (lcd >= 0) and lcd or 0

    if not any then
        return child, false
    end

    local abbr = child:sub(1, lcd) .. first_letter(child:sub(lcd + 1))
    return abbr, abbr ~= child
end

--------------------------------------------------------------------------------
--- -name:  os.abbreviatepath
--- -ver:   1.4.1
--- -arg:   path:string
--- -arg:   [decide:function]
--- -arg:   [transform:function]
--- -ret:   string
--- This abbreviates parent directories in <span class="arg">path</span> to the
--- shortest string that uniquely identifies the directory.  The drive, if
--- present, is never abbreviated.  By default, the first and last names in the
--- string are not abbreviated.
---
--- For performance reasons, UNC paths and paths on remote or removeable drives
--- are never abbreviated.
---
--- The return value is the resulting path after abbreviation.
---
--- If an optional <span class="arg">decide</span> function is provided then
--- it is called once for each directory name in <span class="arg">path</span>,
--- and it can control which directories get abbreviated.  The
--- <span class="arg">decide</span> function receives one argument: the path to
--- the directory that may be abbreviated.  The function can return true to try
--- to abbreviate the directory, or false to prevent abbreviating the directory,
--- or nil to allow the default behavior (don't abbreviate the first or last
--- directory names).
---
--- If an optional <span class="arg">transform</span> function is provided then
--- it is called once for each directory name in the resulting string.  The
--- <span class="arg">transform</span> function receives two arguments: the
--- string to be appended, and a boolean indicating whether it has been
--- abbreviated.  The function can adjust the string, and should return a string
--- to appended.  If it returns nil, the string is appended as-is.  This is
--- intended to be able to apply ANSI escape color codes, for example to
--- highlight special directories or to show which directory names have been
--- abbreviated.
---
--- This function can potentially take some time to complete, but it can be
--- called in a coroutine and yields appropriately.  It is the caller's
--- responsibility to ensure that any <span class="arg">decide</span> or
--- <span class="arg">transform</span> functions are either very fast, yield
--- appropriately, or are not used from a coroutine.
--- -show:  -- Suppose only the following directories exist in the D:\xyz directory:
--- -show:  --  - D:\xyz\bag
--- -show:  --  - D:\xyz\bookkeeper
--- -show:  --  - D:\xyz\bookkeeping
--- -show:  --  - D:\xyz\box
--- -show:  --  - D:\xyz\boxes
--- -show:  --  - D:\xyz\nonrepo
--- -show:  --  - D:\xyz\repo
--- -show:
--- -show:  os.abbreviatepath("d:\\xyz")                        -- returns "d:\\xyz"
--- -show:  os.abbreviatepath("d:\\xyz\\bag")                   -- returns "d:\\xyz\\bag"
--- -show:  os.abbreviatepath("d:\\xyz\\bag\\subdir")           -- returns "d:\\xyz\\ba\\subdir"
--- -show:  os.abbreviatepath("d:\\xyz\\bookkeeper")            -- returns "d:\\xyz\\bookkeeper"
--- -show:  os.abbreviatepath("d:\\xyz\\bookkeeper\\subdir")    -- returns "d:\\xyz\\bookkeepe\\subdir"
--- -show:  os.abbreviatepath("d:\\xyz\\bookkeeping\\file")     -- returns "d:\\xyz\\bookkeepi\\file"
--- -show:  os.abbreviatepath("d:\\xyz\\box\\subdir")           -- returns "d:\\xyz\\box\\subdir"
--- -show:  os.abbreviatepath("d:\\xyz\\boxes\\file")           -- returns "d:\\xyz\\boxe\\file"
--- -show:
--- -show:  -- Examples with a `decide` function.
--- -show:
--- -show:  local function not_git_dir(dir)
--- -show:  &nbsp;   if os.isdir(path.join(dir, ".git")) then
--- -show:  &nbsp;       return false -- Don't abbreviate git repo directories.
--- -show:  &nbsp;   end
--- -show:  end
--- -show:
--- -show:  os.abbreviatepath("d:\\xyz\\nonrepo\\subdir", not_git_dir)  -- returns "d:\\xyz\\n\\subdir"
--- -show:  os.abbreviatepath("d:\\xyz\\repo\\subdir", not_git_dir)     -- returns "d:\\xyz\\repo\\subdir"
--- -show:
--- -show:  -- Example with a `decide` function and a `transform` function.
--- -show:
--- -show:  local function do_all(dir)
--- -show:  &nbsp;   return true
--- -show:  end
--- -show:
--- -show:  local function dim_abbrev(name, abbrev)
--- -show:  &nbsp;   if abbrev then
--- -show:  &nbsp;       return "\027[38m"..name.."\027[m" -- Use dark gray text.
--- -show:  &nbsp;   else
--- -show:  &nbsp;       return name
--- -show:  &nbsp;   end
--- -show:  end
--- -show:
--- -show:  os.abbreviatepath("d:\\xyz\\bag", do_all, dim_abbrev)
--- -show:  -- returns "c:\\\027[38mx\027[m\\\027[38mba\027[m"
--- -show:
--- -show:  -- Relative paths work as well.
--- -show:
--- -show:  os.chdir("d:\\xyz")
--- -show:  os.abbreviatesetcurrentdir("bag\\subdir", true)     -- returns "ba\\s" (if "subdir" is unique)
function os.abbreviatepath(dir, decide, transform)
    -- Removeable drives could be floppy disks or CD-ROMs, which are slow.
    -- Network drives are slow.  Invalid drives are unknown.  If the drive
    -- type might be slow then don't abbreviate.
    local tilde, tilde_len = dir:find("^~[/\\]+")
    local s, parent

    local drivetype, parse
    local drive = path.getdrive(dir) or ""
    if tilde then
        parent = os.getenv("HOME")
        drive = path.getdrive(parent) or ""
        drivetype = os.getdrivetype(drive)
        s = "~"
        parse = dir:sub(tilde_len + 1)
    elseif drive ~= "" then
        local seps
        parent = drive
        drivetype = os.getdrivetype(drive)
        seps, parse = dir:match("^([/\\]*)(.*)$", #drive + 1)
        s = drive
        if #seps > 0 then
            parent = parent .. "\\"
            s = s .. "\\"
        end
    else
        parent = os.getcwd()
        drive = path.getdrive(parent) or ""
        drivetype = os.getdrivetype(drive)
        s = ""
        parse = dir
    end

    if drivetype ~= "fixed" and drivetype ~= "ramdisk" then
        return dir
    end

    local components = {}
    while true do
        local up, child = path.toparent(parse)
        if #child > 0 then
            table.insert(components, child .. "\\")
        else
            if #up > 0 then
                table.insert(components, up)
            end
            break
        end
        parse = up
    end

    local first = #components
    if tilde then
        first = first - 1
    end

    for i = first, 1, -1 do
        local child = components[i]
        local this_dir = path.join(parent, child)
        local abbrev, did
        if decide then
            abbrev = decide(this_dir)
        end
        if abbrev == nil then
            abbrev = not (i == 1 or i == #components)
        end
        if abbrev then
            child, did = abbrev_child(parent, child)
        end
        if transform then
            child = transform(child, did) or child
        end
        s = path.join(s, child)
        parent = this_dir
    end

    local ret = s:gsub("[/\\]+$", "")
    return ret
end
