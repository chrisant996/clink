local prev_dir      -- Most recent git repo visited.
local prev_info     -- Most recent info retrieved by the coroutine.

local function get_git_dir(dir)
    -- Check if the current directory is in a git repo.
    local child
    repeat
        if os.isdir(path.join(dir, ".git")) then
            return dir
        end
        -- Walk up one level to the parent directory.
        dir,child = path.toparent(dir)
        -- If child is empty, we've reached the top.
    until (not child or child == "")
    return nil
end

local function get_git_branch()
    -- Get the current git branch name.
    local file = io.popen("git branch --show-current 2>nul")
    local branch = file:read("*a"):match("(.+)\n")
    file:close()
    return branch
end

local function get_git_status()
    -- The io.popenyield API is like io.popen, but it yields until the output is
    -- ready to be read.
    local file = io.popenyield("git --no-optional-locks status --porcelain 2>nul")
    local status = false
    for line in file:lines() do
        -- If there's any output, the status is not clean.  Since this example
        -- doesn't analyze the details, it can stop once it knows there's any
        -- output at all.
        status = true
        break
    end
    file:close()
    return status
end

local function get_git_conflict()
    -- The io.popenyield API is like io.popen, but it yields until the output is
    -- ready to be read.
    local file = io.popenyield("git diff --name-only --diff-filter=U 2>nul")
    local conflict = false
    for line in file:lines() do
        -- If there's any output, there's a conflict.
        conflict = true
        break
    end
    file:close()
    return conflict
end

local function collect_git_info()
    -- This is run inside the coroutine, which happens while idle while waiting
    -- for keyboard input.
    local info = {}
    info.status = get_git_status()
    info.conflict = get_git_conflict()
    -- Until this returns, the call to clink.promptcoroutine() will keep
    -- returning nil.  After this returns, subsequent calls to
    -- clink.promptcoroutine() will keep returning this return value, until a
    -- new input line begins.
    return info
end

local git_prompt = clink.promptfilter(55)
function git_prompt:filter(prompt)
    -- Do nothing if not a git repo.
    local dir = get_git_dir(os.getcwd())
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
    local branch = get_git_branch()
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
    -- Choose color for the git branch name:  green if status is clean, yellow
    -- if status is not clean, red if conflict is present, or default color if
    -- status isn't known yet.
    local sgr = "37;1"
    if info.conflict then
        sgr = "31;1"
    elseif info.status ~= nil then
        sgr = info.status and "33;1" or "32;1"
    end
    -- Prefix the prompt with "[branch]" using the status color.
    return "\x1b["..sgr.."m["..branch.."]\x1b[m  "..prompt
end
