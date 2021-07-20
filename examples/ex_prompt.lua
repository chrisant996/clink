local green  = "\x1b[92m"
local yellow = "\x1b[93m"
local cyan   = "\x1b[36m"
local normal = "\x1b[m"

-- A prompt filter that discards any prompt so far and sets the
-- prompt to the current working directory.  An ANSI escape code
-- colors it yellow.
local cwd_prompt = clink.promptfilter(30)
function cwd_prompt:filter(prompt)
    return yellow..os.getcwd()..normal
end

-- A prompt filter that inserts the date at the beginning of the
-- the prompt.  An ANSI escape code colors the date green.
local date_prompt = clink.promptfilter(40)
function date_prompt:filter(prompt)
    return green..os.date("%a %H:%M")..normal.." "..prompt
end

-- A prompt filter that may stop further prompt filtering.
-- This is a silly example, but on Wednesdays, it stops the
-- filtering, which in this example prevents git branch
-- detection and the line feed and angle bracket.
local wednesday_silliness = clink.promptfilter(45)
function wednesday_silliness:filter(prompt)
    if os.date("%a") == "Wed" then
        -- The ,false stops any further filtering.
        return prompt.." HAPPY HUMP DAY! ", false
    end
end

-- A prompt filter that appends the current git branch.
local git_branch_prompt = clink.promptfilter(50)
function git_branch_prompt:filter(prompt)
    local line = io.popen("git branch --show-current 2>nul"):read("*a")
    local branch = line:match("(.+)\n")
    if branch then
        return prompt.." "..cyan.."["..branch.."]"..normal
    end
end

-- A prompt filter that adds a line feed and angle bracket.
local bracket_prompt = clink.promptfilter(150)
function bracket_prompt:filter(prompt)
    return prompt.."\n> "
end
