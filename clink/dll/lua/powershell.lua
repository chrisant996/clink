-- Copyright (c) 2013 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function powershell_prompt_filter()
    local l, r, path = clink.prompt.value:find("([a-zA-Z]:\\.*)> $")
    if path ~= nil then
        os.chdir(path)
    end
end

--------------------------------------------------------------------------------
if clink.get_host_process() == "powershell.exe" then
    clink.prompt.register_filter(powershell_prompt_filter, -493)
end
