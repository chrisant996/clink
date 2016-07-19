-- Copyright (c) 2013 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function powershell_prompt_filter(prompt)
    local l, r, path = prompt:find("([a-zA-Z]:\\.*)> $")
    if path ~= nil then
        os.chdir(path)
    end
end

--------------------------------------------------------------------------------
prompt.register_filter(powershell_prompt_filter, -493)
