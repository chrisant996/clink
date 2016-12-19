-- Copyright (c) 2013 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local powershell_prompt = clink.promptfilter(-493)

--------------------------------------------------------------------------------
function powershell_prompt:filter(prompt)
    local l, r, path = prompt:find("([a-zA-Z]:\\.*)> $")
    if path ~= nil then
        os.chdir(path)
    end
end

