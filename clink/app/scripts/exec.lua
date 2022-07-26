-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
settings.add("exec.enable", true, "Enable executable matching for 'complete'",
[[When enabled, the completion commands only match executables and directories
when completing the first word of a line.]])

settings.add("exec.path", true, "Match executables in PATH",
[[Completes executables found in the directories specified in the PATH
environment system variable.  (See exec.enable)]])

settings.add("exec.aliases", true, "Include aliases",
[[Include doskey aliases as matches.  (See exec.enable)]])

settings.add("exec.commands", true, "Include CMD commands",
[[Include CMD commands.  (See exec.enable)]])

settings.add("exec.cwd", true, "Match executables in current directory",
[[Include executables in the current directory.  This is implicit if the word
being completed is a relative path.  (See exec.enable)]])

settings.add("exec.dirs", true, "Include directories",
[[Include directories relative to the current working directory as matches.
(See exec.enable)]])

settings.add("exec.files", false, "Include files",
[[Include files in the current working directory as matches.  This includes
executables in the current directory even when exec.cwd is off.  (See
exec.enable, and exec.cwd)]])

settings.add("exec.space_prefix", true, "Whitespace prefix matches files",
[[If the line begins with whitespace then Clink bypasses executable matching
and will do normal files matching instead.  (See exec.enable)]])

--------------------------------------------------------------------------------
local function get_environment_paths()
    local paths = (os.getenv("path") or ""):explode(";")

    -- Append slashes.
    for i = 1, #paths, 1 do
        paths[i] = paths[i].."\\"
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
    local endword = line_state:getendword()
    if line_state:getwordcount() > 1 or endword == "~" then
        return false
    elseif endword == "." or endword == ".." then
        -- This is to mimic how bash seems to work when completing `.` or `..`
        -- as the first word in a command line.
        -- See https://github.com/chrisant996/clink/issues/111.
        match_builder:addmatch({ match = endword, type = "dir" })
        return true
    end

    -- If enabled, lines prefixed with whitespace disable executable matching.
    if settings.get("exec.space_prefix") then
        local offset = line_state:getcommandoffset()
        if line_state:getline():sub(offset, offset):find("[ \t]") then
            return false
        end
    end

    -- Settings that control what matches are generated.
    local match_dirs = settings.get("exec.dirs")
    local match_cwd = settings.get("exec.cwd")

    local paths = nil
    local text, expanded = rl.expandtilde(line_state:getword(1))
    local text_dir = (path.getdirectory(text) or ""):gsub("/", "\\")
    if #text_dir == 0 then
        -- Add console aliases as matches.
        if settings.get("exec.aliases") then
            local aliases = os.getaliases()
            match_builder:addmatches(aliases, "alias")
        end

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

    local co, ismain = coroutine.running()

    local add_files = function(pattern, rooted)
        local any_added = false
        if ismain or os.getdrivetype(pattern) ~= "remote" then
            local root = nil
            if rooted then
                root = (path.getdirectory(pattern) or ""):gsub("/", "\\")
                if expanded then
                    root = rl.collapsetilde(root)
                end
            end
            for _, f in ipairs(os.globfiles(pattern, true)) do
                local file = (root and path.join(root, f.name)) or f.name
                any_added = match_builder:addmatch({ match = file, type = f.type }) or any_added
            end
        end
        return any_added
    end

    -- Include files.
    if settings.get("exec.files") then
        match_cwd = false
        added = add_files(text.."*", true) or added
    end

    -- Search 'paths' for files ending in 'suffices' and look for matches.
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
        local root = (path.getdirectory(text) or ""):gsub("/", "\\")
        if expanded then
            root = rl.collapsetilde(root)
        end
        for _, d in ipairs(os.globdirs(text.."*", true)) do
            local dir = path.join(root, d.name)
            match_builder:addmatch({ match = dir, type = d.type })
        end
    end

    return true
end
