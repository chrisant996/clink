-- Copyright (c) 2024 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- NOTE: If you add any settings here update set.cpp to load (lua, lib, hinter).

--------------------------------------------------------------------------------
clink = clink or {}
local _hinters = {}
local _hinters_unsorted = false

if settings.get("lua.debug") or clink.DEBUG then
    -- Make it possible to inspect these locals in the debugger.
    clink.debug = clink.debug or {}
    clink.debug._hinters = _hinters
end


--------------------------------------------------------------------------------
local function prepare()
    -- Sort hinters by priority if required.
    if _hinters_unsorted then
        local lambda = function(a, b) return a._priority < b._priority end
        table.sort(_hinters, lambda)

        _hinters_unsorted = false
    end
end

--------------------------------------------------------------------------------
local elapsed_this_pass = 0
local force_diag_hinters
local function log_cost(tick, hinter)
    local elapsed = (os.clock() - tick) * 1000
    local cost = hinter.cost
    if not cost then
        cost = { last=0, total=0, num=0, peak=0 }
        hinter.cost = cost
    end

    cost.last = elapsed
    cost.total = cost.total + elapsed
    cost.num = cost.num + 1
    if cost.peak < elapsed then
        cost.peak = elapsed
    end

    elapsed_this_pass = elapsed_this_pass + elapsed
end

--------------------------------------------------------------------------------
-- Receives a line_state_lua.
function clink._gethint(line_state)
    local impl = function ()
        local bestpos
        local besthint
        local cursorpos = line_state:getcursor()

        elapsed_this_pass = 0

        for _, hinter in ipairs(_hinters) do
            if hinter.gethint then
                line_state:_reset_shift()
                local tick = os.clock()
                local hint, pos = hinter:gethint(line_state)
                log_cost(tick, hinter)
                if hint then
                    if not pos then
                        pos = -1
                    end
                    if pos == cursorpos then
                        return hint, pos
                    elseif pos < cursorpos and (not bestpos or bestpos < pos) then
                        besthint = hint
                        bestpos = pos
                    end
                end
            end
        end

        if besthint and besthint:find("[\r\n]") then
            besthint = besthint:gsub("[\r\n]", " ")
        end

        if elapsed_this_pass > 0.05 then
            force_diag_hinters = true
        end

        return besthint, bestpos
    end

    prepare()

    local ok, ret, pos = xpcall(impl, _error_handler_ret)
    if not ok then
        print("")
        print("hinter failed:")
        print(ret)
        return
    end

    return ret, pos
end

--------------------------------------------------------------------------------
--- -name:  clink.hinter
--- -ver:   1.6.22
--- -arg:   [priority:integer]
--- -ret:   table
--- Creates and returns a new hinter object.  Define on the object a
--- <code>:gethint()</code> function which gets called in increasing
--- <span class="arg">priority</span> order (low values to high values) when
--- updating the input line display (e.g. while typing).  See
--- <a href="#showinginputhints">Showing Input Hints</a> for more information.
function clink.hinter(priority)
    if priority == nil then priority = 999 end

    local ret = { _priority = priority }
    table.insert(_hinters, ret)

    _hinters_unsorted = true
    return ret
end

--------------------------------------------------------------------------------
local function pad_string(s, len)
    if #s < len then
        s = s..string.rep(" ", len - #s)
    end
    return s
end

--------------------------------------------------------------------------------
function clink._diag_hinters(arg)
    arg = (arg and arg >= 2)
    if not arg and not settings.get("lua.debug") and not force_diag_hinters then
        return
    end

    local bold = "\x1b[1m"          -- Bold (bright).
    local header = "\x1b[36m"       -- Cyan.
    local norm = "\x1b[m"           -- Normal.

    local any_cost
    local t = {}
    local longest = 24
    for _,hinter in ipairs (_hinters) do
        if hinter.gethint then
            local info = debug.getinfo(hinter.gethint, 'S')
            if not clink._is_internal_script(info.short_src) then
                local src = info.short_src..":"..info.linedefined
                table.insert(t, { src=src, cost=hinter.cost })
                if longest < #src then
                    longest = #src
                end
                if not any_cost and hinter.cost then
                    any_cost = true
                end
            end
        end
    end

    if t[1] then
        if any_cost then
            clink.print(string.format("%s%s%s     %slast    avg     peak%s",
                    bold, pad_string("hinters:", longest + 2), norm,
                    header, norm))
        else
            clink.print(bold.."hinters:"..norm)
        end
        for _,entry in ipairs (t) do
            if entry.cost then
                clink.print(string.format("  %s  %4u ms %4u ms %4u ms",
                        pad_string(entry.src, longest),
                        entry.cost.last, entry.cost.total / entry.cost.num, entry.cost.peak))
            else
                clink.print(string.format("  %s", entry.src))
            end
        end
    end
end
