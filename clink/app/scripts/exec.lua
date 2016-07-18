-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
settings.add("exec.enable", true, "Enable executable matching",
"Only match executables when completing the first word of a line")

settings.add("exec.path", true, "Match executables in PATH",
[[Completes execuables found in the directories specified in the PATH
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
local dos_commands = {
    "assoc", "break", "call", "cd", "chcp", "chdir", "cls", "color", "copy",
    "date", "del", "dir", "diskcomp", "diskcopy", "echo", "endlocal", "erase",
    "exit", "for", "format", "ftype", "goto", "graftabl", "if", "md", "mkdir",
    "mklink", "more", "move", "path", "pause", "popd", "prompt", "pushd", "rd",
    "rem", "ren", "rename", "rmdir", "set", "setlocal", "shift", "start",
    "time", "title", "tree", "type", "ver", "verify", "vol"
}

--------------------------------------------------------------------------------
local function get_environment_paths()
    local paths = clink.split(os.getenv("path"), ";")

    -- We're expecting absolute paths and as ';' is a valid path character
    -- there maybe unneccessary splits. Here we resolve them.
    local paths_merged = { paths[1] }
    for i = 2, #paths, 1 do
        if not paths[i]:find("^[a-zA-Z]:") then
            local t = paths_merged[#paths_merged]
            paths_merged[#paths_merged] = t..paths[i]
        else
            table.insert(paths_merged, paths[i])
        end
    end

    -- Append slashes.
    for i = 1, #paths_merged, 1 do
        paths_merged[i] = paths_merged[i].."/"
    end

    return paths_merged
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
local function exec_match_generator(line_state, match_builder)
    -- If executable matching is disabled do nothing
    if not settings.get("exec.enable") then
        return false
    end

    -- We're only interested in exec completion if this is the first word of
    -- the line.
    if line_state:getwordcount() > 1 then
        return false
    end

    -- If enabled, lines prefixed with
    if settings.get("exec.space_prefix") then
        local word_info = line_state:getwordinfo(1)
        local offset = 1
        if word_info.quoted then offset = 2 end
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
        -- If the terminal is cmd.exe check it's commands for matches.
        if clink.get_host_process() == "cmd.exe" then
            match_builder:add(dos_commands)
        end

        -- Add console aliases as matches.
        local aliases = clink.get_console_aliases()
        match_builder:add(aliases)

        -- Add environment's PATH variable as paths to search.
        if settings.get("exec.path") then
            paths = get_environment_paths()
        end
    else
        -- 'text' is an absolute or relative path so override settings and
        -- match current directory and it's directories too.
        match_dirs = true
        match_cwd = true
    end

    if not paths then
        paths = {}
    end

    -- Should we also consider the path referenced by 'text'?
    if match_cwd then
        table.insert(paths, text)
    end

    -- Search 'paths' for files ending in 'suffices' and look for matches
    local added = false
    local suffices = clink.split(os.getenv("pathext"), ";")
    for _, suffix in ipairs(suffices) do
        for _, dir in ipairs(paths) do
            for _, file in ipairs(os.globfiles(dir.."*"..suffix)) do
                added = match_builder:add(file) or added
            end
        end
    end

    -- Lastly we may wish to consider directories too.
    if match_dirs or not added then
        match_builder:add(os.globdirs(text.."*"))
    end

    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(exec_match_generator, 50)
