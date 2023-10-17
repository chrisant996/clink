settings.add("fzf.height", "40%", "Height to use for the --height flag")
settings.add("fzf.exe_location", "", "Location of fzf.exe if not on the PATH")

-- Build a command line to launch fzf.
local function get_fzf()
    local height = settings.get("fzf.height")
    local command = settings.get("fzf.exe_location")
    if not command or command == "" then
        command = "fzf.exe"
    else
        command = os.getshortname(command)
    end
    if height and height ~= "" then
        command = command..' --height '..height
    end
    return command
end

local fzf_complete_intercept = false

-- Sample key binding in .inputrc:
--      M-C-x: "luafunc:fzf_complete"
function fzf_complete(rl_buffer)
    fzf_complete_intercept = true
    rl.invokecommand("complete")
    if fzf_complete_intercept then
        rl_buffer:ding()
    end
    fzf_complete_intercept = false
    rl_buffer:refreshline()
end

local function filter_matches(matches, completion_type, filename_completion_desired)
    if not fzf_complete_intercept then
        return
    end
    -- Start fzf.
    local r,w = io.popenrw(get_fzf()..' --layout=reverse-list')
    if not r or not w then
        return
    end
    -- Write matches to the write pipe.
    for _,m in ipairs(matches) do
        w:write(m.match.."\n")
    end
    w:close()
    -- Read filtered matches.
    local ret = {}
    while (true) do
        local line = r:read('*line')
        if not line then
            break
        end
        for _,m in ipairs(matches) do
            if m.match == line then
                table.insert(ret, m)
            end
        end
    end
    r:close()
    -- Yay, successful; clear it to not ding.
    fzf_complete_intercept = false
    return ret
end

local interceptor = clink.generator(0)
function interceptor:generate(line_state, match_builder)
    -- Only intercept when the specific key binding command was used.
    if fzf_complete_intercept then
        clink.onfiltermatches(filter_matches)
    end
    return false
end
