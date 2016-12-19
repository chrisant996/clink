-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink.argmatcher("set"):addarg(
    function ()
        local ret = {}
        for _, i in ipairs(os.getenvnames()) do
            table.insert(ret, { match = i, suffix = "=" })
        end

        return ret
    end
)
