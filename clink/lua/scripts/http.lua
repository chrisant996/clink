-- Copyright (c) 2025 Christopher Antos
-- License: http://opensource.org/licenses/MIT

-- luacheck: globals http
http = http or {}

--------------------------------------------------------------------------------
--- -name:  http.get
--- -var:   1.9.0
http.get = function (url, options)
    local user_agent = options and options.user_agent or nil
    local no_cache = options and options.no_cache or false
    local ay, req = http._get_internal(url, user_agent, no_cache)
-- TODO:  Error reporting.
    if req then
        if ay then
            while not ay:ready() do
                coroutine.yield()
            end
        end
        return req:result()
    end
end
