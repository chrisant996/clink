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
local function exec_match_generator(text, first, last, result)
    -- If match style setting is < 0 then consider executable matching disabled.
    local enabled = settings.get("exec.enable")
    if not enabled then
        return false
    end

    -- We're only interested in exec completion if this is the first word of the
    -- line, or the first word after a command separator.
    if settings.get("exec.space_prefix") then
        if first > 1 then
            return false
        end
    else
        local leading = line_state.line:sub(1, first - 1)
        local is_first = leading:find("^%s*\"*$")
        if not is_first then
            return false
        end
    end

    -- Settings that control what matches are generated.
    local match_dirs = settings.get("exec.dirs")
    local match_cwd = settings.get("exec.cwd")

    local paths = nil
    local text_dir = path.getdirectory(text) or ""
    if #text_dir == 0 then
        -- If the terminal is cmd.exe check it's commands for matches.
        if clink.get_host_process() == "cmd.exe" then
            result:addmatches(dos_commands)
        end

        -- Add console aliases as matches.
        local aliases = clink.get_console_aliases()
        result:addmatches(aliases)

        if settings.get("exec.path") then
            paths = get_environment_paths()
        end
    else
        -- 'text' is an absolute or relative path so override settings and
        -- match current directory and it's directories too.
        match_dir = true
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
    local suffices = clink.split(os.getenv("pathext"), ";")
    for _, suffix in ipairs(suffices) do
        for _, dir in ipairs(paths) do
            for _, file in ipairs(os.globfiles(dir.."*"..suffix)) do
                file = path.getname(file)
                result:addmatch(path.join(text_dir, file))
            end
        end
    end

    -- Lastly we may wish to consider directories too.
    if result:getmatchcount() == 0 or match_dirs then
        result:addmatches(os.globdirs(text.."*"))
    end

    result:arefiles()
    return true
end

--------------------------------------------------------------------------------
clink.register_match_generator(exec_match_generator, 50)
