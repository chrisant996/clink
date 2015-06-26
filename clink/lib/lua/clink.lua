--
-- Copyright (c) 2012 Martin Ridgers
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

--------------------------------------------------------------------------------
function clink.is_point_in_quote(str, i)
    if i > #str then
        i = #str
    end

    local c = 1
    local q = string.byte("\"")
    for j = 1, i do
        if string.byte(str, j) == q then
            c = c * -1
        end
    end

    if c < 0 then
        return true
    end

    return false
end

--------------------------------------------------------------------------------
function clink.adjust_for_separator(buffer, cursor, first, last)
    local seps = nil
    if clink.get_host_process() == "cmd.exe" then
        seps = "|&"
    end

    if seps then
        -- Find any valid command separators and if found, manipulate the
        -- completion state a little bit.
        local leading = buffer:sub(1, first - 1)

        -- regex is: <sep> <not_seps> <eol>
        local regex = "["..seps.."]([^"..seps.."]*)$"
        local sep_found, _, post_sep = leading:find(regex)

        if sep_found and not clink.is_point_in_quote(leading, sep_found) then
            local delta = #leading - #post_sep
            buffer = buffer:sub(delta + 1)
            first = first - delta
            last = last - delta
            cursor = cursor - delta

            if first < 1 then
                first = 1
            end
        end
    end

    return buffer, cursor, first, last
end

--------------------------------------------------------------------------------
function clink.split(str, sep)
    local i = 1
    local ret = {}
    for _, j in function() return str:find(sep, i, true) end do
        table.insert(ret, str:sub(i, j - 1))
        i = j + 1
    end
    table.insert(ret, str:sub(i, j))

    return ret
end

--------------------------------------------------------------------------------
function clink.quote_split(str, ql, qr)
    if not qr then
        qr = ql
    end

    -- First parse in "pre[ql]quote_string[qr]" chunks
    local insert = table.insert
    local i = 1
    local needle = "%b"..ql..qr
    local parts = {}
    for l, r, quote in function() return str:find(needle, i) end do
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
