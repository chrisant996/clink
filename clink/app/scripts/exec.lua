-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
settings.add("exec.enable", true, "Enable executable matching for 'complete'",
[[When enabled, the completion commands only match executables and directories
when completing the first word of a line.]])

settings.add("exec.path", true, "Match executables in PATH",
[[Completes executables found in the directories specified in the PATH
environment system variable.]])

settings.add("exec.cwd", false, "Match executables in current directory",
[[Include executables in the current directory. This is implicit if the word
being completed is a relative path.]])

settings.add("exec.dirs", true, "Include directories",
"Include directories relative to the current working directory as matches.")

settings.add("exec.space_prefix", true, "Whitespace prefix matches files",
[[If the line begins with whitespace then Clink bypasses executable
matching and will do normal files matching instead.]])

--------------------------------------------------------------------------------
local function get_environment_paths()
    local paths = os.getenv("path"):explode(";")

    -- Append slashes.
    for i = 1, #paths, 1 do
        paths[i] = paths[i].."/"
    end

    return paths
end

--------------------------------------------------------------------------------
local function exec_find_dirs(pattern, case_map)
    local ret = {}

    for _, dir in ipairs(clink.find_dirs(pattern, case_map)) do
        if dir ~= "." and dir ~= ".." then
            table.insert(ret, dir)
        end
    end

    return ret
end

--------------------------------------------------------------------------------
local exec_generator = clink.generator(50)

function exec_generator:generate(line_state, match_builder)
    -- If executable matching is disabled do nothing
    if not settings.get("exec.enable") then
        return false
    end

    -- We're only interested in exec completion if this is the first word of
    -- the line.
    if line_state:getwordcount() > 1 then
        return false
    end

    -- If enabled, lines prefixed with whitespace disable executable matching.
    if settings.get("exec.space_prefix") then
        local word_info = line_state:getwordinfo(1)
        local offset = line_state:getcommandoffset()
        if word_info.quoted then offset = offset + 1 end
        if word_info.offset > offset then
            return false
        end
    end

    -- Settings that control what matches are generated.
    local match_dirs = settings.get("exec.dirs")
    local match_cwd = settings.get("exec.cwd")

    local paths = nil
    local text = line_state:getword(1)
    local text_dir = path.getdirectory(text) or ""
    if #text_dir == 0 then
        -- Add console aliases as matches.
        local aliases = os.getaliases()
        match_builder:addmatches(aliases, "alias")

        -- Add environment's PATH variable as paths to search.
        if settings.get("exec.path") then
            paths = get_environment_paths()
        end
    else
        -- 'text' is an absolute or relative path so override settings and
        -- match current directory and its directories too.
        match_dirs = true
        match_cwd = true
    end

    if not paths then
        paths = {}
    end

    local add_files = function(pattern, rooted)
        local any_added = false
        local root = nil
        if rooted then
            root = path.getdirectory(pattern) or ""
        end
        for _, f in ipairs(os.globfiles(pattern)) do
-- TODO: PERFORMANCE: globfiles should return whether each file is hidden since
-- it already had that knowledge.
            local file = (root and path.join(root, f)) or f
            if os.ishidden(file) then
                any_added = match_builder:addmatch({ match = file, type = "file,hidden" }) or any_added
            else
                any_added = match_builder:addmatch({ match = file, type = "file" }) or any_added
            end
        end
        return any_added
    end

    -- Search 'paths' for files ending in 'suffices' and look for matches
    local added = false
    local suffices = (os.getenv("pathext") or ""):explode(";")
    for _, suffix in ipairs(suffices) do
        for _, dir in ipairs(paths) do
            added = add_files(dir.."*"..suffix) or added
        end

        -- Should we also consider the path referenced by 'text'?
        if match_cwd then
            -- Pass true because these need to include the base path.
            added = add_files(text.."*"..suffix, true) or added
        end
    end

    -- Lastly we may wish to consider directories too.
    if match_dirs or not added then
        local root = path.getdirectory(text) or ""
        for _, d in ipairs(os.globdirs(text.."*")) do
-- TODO: PERFORMANCE: globdirs should return whether each dir is hidden since
-- it already had that knowledge.
            local dir = path.join(root, d)
            if os.ishidden(dir) then
                match_builder:addmatch({ match = dir, type = "dir,hidden" })
            else
                match_builder:addmatch({ match = dir, type = "dir" })
            end
        end
    end

    return true
end
