local prev_dir      -- Most recent git repo visited.
local prev_info     -- Most recent info retrieved by the coroutine.

local function collect_git_info()
    -- This is run inside the coroutine, which happens while idle while waiting
    -- for keyboard input.
    local info = {}
    info.status = git.getstatus()
    -- Until this returns, the call to clink.promptcoroutine() will keep
    -- returning nil.  After this returns, subsequent calls to
    -- clink.promptcoroutine() will keep returning this return value, until a
    -- new input line begins.
    return info
end

local git_prompt = clink.promptfilter(55)

function git_prompt:filter(prompt)
    -- Do nothing if not a git repo.
    local dir = git.getgitdir()
    if not dir then
        return
    end

    -- Reset the cached status if in a different repo.
    if prev_dir ~= dir then
        prev_info = nil
        prev_dir = dir
    end

    -- Do nothing if git branch not available.  Getting the branch name is fast,
    -- so it can run outside the coroutine.  That way the branch name is visible
    -- even while the coroutine is running.
    local branch = git.getbranch()
    if not branch or branch == "" then
        return
    end

    -- Start a coroutine to collect various git info in the background.  The
    -- clink.promptcoroutine() call returns nil immediately, and the
    -- coroutine runs in the background.  After the coroutine finishes, prompt
    -- filtering is triggered again, and subsequent clink.promptcoroutine()
    -- calls from this prompt filter immediately return whatever the
    -- collect_git_info() function returned when it completed.  When a new input
    -- line begins, the coroutine results are reset to nil to allow new results.
    local info = clink.promptcoroutine(collect_git_info)
    -- If no status yet, use the status from the previous prompt.
    if info == nil then
        info = prev_info or {}
    else
        prev_info = info
    end

    -- Prefer using the branch name from git.getstatus(), once it's available.
    if info.status and info.status.branch then
        branch = info.status.branch
    end

    -- Choose color for the git branch name.:  green if status is clean, yellow
    -- if status is not clean, red if conflict is present, or default color if
    -- status isn't known yet.
    local sgr
    if not info.status then
        sgr = "39"          -- Default color when status not available yet.
    elseif info.status.conflict then
        sgr = "31;1"        -- Red when conflict.
    elseif info.status.dirty then
        sgr = "33;1"        -- Yellow when dirty (changes are present).
    else
        sgr = "32;1"        -- Green when status is clean.
    end

    -- Prefix the prompt with "[branch]" using the status color.
    -- NOTE:  This is for illustration purposes and works when no other custom
    -- prompt filters are installed.  If another custom prompt filter is present
    -- and runs earlier and chooses to halt further prompt filtering, then this
    -- example code might not get reached.
    return "\x1b["..sgr.."m["..branch.."]\x1b[m  "..prompt
end
