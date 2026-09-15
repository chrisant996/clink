-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

local internal = import_internal -- luacheck: no global

--------------------------------------------------------------------------------
settings.add("exec.enable", true, "Enable executable completions",
[[When enabled, the completion commands only match executables and directories
when completing the first word of a line.]])

settings.add("exec.path", true, "Match executables in PATH",
[[Completes executables found in the directories specified in the PATH
environment variable.  (See exec.enable)]])

settings.add("exec.aliases", true, "Include aliases",
[[Include doskey aliases as matches.  (See exec.enable)]])

settings.add("exec.commands", true, "Include CMD commands",
[[Include CMD commands.  (See exec.enable)]])

settings.add("exec.cwd", true, "Match executables in current directory",
[[Include executables in the current directory.  This is implicit if the word
being completed is a relative path.  (See exec.enable)]])

settings.add("exec.cwd_first", false, "Sort matches from current dir first",
[[When enabled, matches from the current directory are listed first, followed by
the other matches (such as doskey aliases, cmd commands, files found along the
PATH environment variable, etc).]])

settings.add("exec.dirs", true, "Include directories",
[[Include directories relative to the current working directory as matches.
(See exec.enable)]])

settings.add("exec.files", false, "Include files",
[[Include files in the current working directory as matches.  This includes
executables in the current directory even when exec.cwd is off.  (See
exec.enable, and exec.cwd)]])

settings.add("exec.associations", false, "Include launchable documents",
[[Include files with registered file associations as matches (i.e. launchable
documents such as '*.pdf' files).  (See exec.enable)]])

settings.add("exec.space_prefix", true, "Whitespace prefix matches files",
[[If the line begins with whitespace then Clink bypasses executable matching
and will do normal files matching instead.  (See exec.enable)]])

--------------------------------------------------------------------------------
local function add_commands(out, line_state, chained)
    -- Cmd commands cannot be quoted.
    local word_info = line_state:getwordinfo(line_state:getwordcount())
    if word_info.quoted then
        return
    end

    -- They should be skipped if the line's whitespace prefixed.
    if not chained and settings.get("exec.space_prefix") then
        local offset = line_state:getcommandoffset()
        if line_state:getline():sub(offset, offset):find("[ \t]") then
            return
        end
    end

    -- If the word being completed is a relative path then commands don't apply.
    local text = line_state:getendword()
    local text_dir = path.getdirectory(text) or ""
    if #text_dir ~= 0 then
        return
    end

    out:addmatches(internal._get_cmd_commands(), "cmd")
end

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
local function comparator(a, b)
    return string.comparematches(a.match, a.type, b.match, b.type)
end

--------------------------------------------------------------------------------
local exec_generator = clink.generator(50)

local function exec_matches(line_state, match_builder, chained, no_aliases)
    -- If executable matching is disabled do nothing.
    if not settings.get("exec.enable") then
        return false
    end

    -- Optionally sort matches from the current directory ahead of other
    -- matches.  That's accomplished by collecting matches into two buckets
    -- and then sorting the buckets separately.
    local cwd_first = settings.get("exec.cwd_first")
    local dots = match_builder
    local first = match_builder
    local second = match_builder
    if cwd_first then
        first = {}
        second = {}
        function first:addmatch(m)
            table.insert(self, m)
        end
        function first:addmatches(matches, t)
            for _, m in ipairs(matches) do
                if type(m) ~= "table" then
                    m = { match=m }
                end
                if not m.type then
                    m.type = t
                end
                table.insert(self, m)
            end
        end
        second.addmatch = first.addmatch
        second.addmatches = first.addmatches
        dots = first
    end

    -- Special cases for "~", ".", and "..".
    local endword = line_state:getendword()
    if endword == "~" then
        return false
    elseif endword == "." or endword == ".." then
        -- This is to mimic how bash seems to work when completing `.` or `..`
        -- as the first word in a command line.
        -- See https://github.com/chrisant996/clink/issues/111.
        if endword == "." then
            dots:addmatch({ match=".\\", type="dir" })
        end
        dots:addmatch({ match="..\\", type="dir" })
    end

    -- If enabled, lines prefixed with whitespace disable executable matching.
    if settings.get("exec.space_prefix") then
        local disable
        if chained then
            local info = line_state:getwordinfo(line_state:getwordcount())
            if info then
                local offset = info.offset - (info.quoted and 2 or 1)
                local prefix = line_state:getline():sub(offset - 1, offset)
                disable = prefix:match("[ \t][ \t]")
            end
        else
            local offset = line_state:getcommandoffset()
            disable = line_state:getline():sub(offset, offset):find("[ \t]")
        end
        if disable then
            -- Be sure to add the dots if they were deferred.
            if cwd_first then
                match_builder:addmatches(dots)
            end
            return false
        end
    end

    -- Settings that control what matches are generated.
    local match_dirs = settings.get("exec.dirs")
    local match_cwd = settings.get("exec.cwd")

    local paths = nil
    local text, expanded = rl.expandtilde(endword) -- luacheck: no unused
    local text_dir = (path.getdirectory(text) or ""):gsub("/", "\\")
    if #text_dir == 0 then
        -- Add console aliases as matches.
        if not no_aliases and settings.get("exec.aliases") then
            local aliases = os.getaliases()
            second:addmatches(aliases, "alias")
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

    local _, ismain = coroutine.running()

    local add_files = function(out, pattern, rooted, only_files)
        local any_added = false
        if ismain or os.getdrivetype(pattern) ~= "remote" then
            -- Use clink.filematches[exact] instead of a custom os.globfiles
            -- loop, so that the behavior is factored.  For example, UNC share
            -- names.  But since clink.filematches is always rooted, it's
            -- necessary to strip the directory when rooted is false (for
            -- matches found through the %PATH% variable).
            local matches = clink.filematchesexact(pattern)
            for _, m in ipairs(matches) do
                if not only_files or m.type:find("file") then
                    if not rooted then
                        m.match = m.match:match("[^/\\]*[/\\]?$")
                    end
                    out:addmatch(m)
                    any_added = true
                end
            end
        end
        return any_added
    end

    local associations = {}
    local add_files_by_association = function(out, pattern, rooted, include_associations) -- luacheck: no unused
        local any_added = false
        if ismain or os.getdrivetype(pattern) ~= "remote" then
            local matches = clink.filematchesexact(pattern)
            for _, m in ipairs(matches) do
                if m.type:find("file") then
                    local ext = path.getextension(m.match):lower()
                    local has = associations[ext]
                    if has == nil and include_associations then
                        has = internal._hasfileassociation(m.match)
                        associations[ext] = has
                    end
                    if has then
                        if not rooted then
                            m.match = m.match:match("[^/\\]*[/\\]?$")
                            m.description = m.description and m.description..", PATH" or "PATH"
                        end
                        out:addmatch(m)
                        any_added = true
                    end
                end
            end
        end
        return any_added
    end

    local added = false

    -- Include commands.
    if settings.get("exec.commands") then
        add_commands(second, line_state, chained)
    end

    -- Include files.
    if settings.get("exec.files") then
        match_cwd = false
        added = add_files(first, endword.."*", true) or added
    end

    -- Search 'paths' for files ending in executable extensions (and/or
    -- registered file associations) and look for matches.
    local suffices = (os.getenv("pathext") or ""):explode(";")
    for _, suffix in ipairs(suffices) do
        associations[suffix:lower()] = true
    end
    local include_associations = settings.get("exec.associations")
    for _, dir in ipairs(paths) do
        added = add_files_by_association(second, dir.."*", false, include_associations) or added
    end

    -- Should we also consider the path referenced by 'text'?
    if match_cwd then
        -- Pass true because these need to include the base path.
        added = add_files_by_association(first, endword.."*", true, include_associations) or added
    end

    -- Lastly we may wish to consider directories too.
    if match_dirs or not added then
        first:addmatches(clink.dirmatchesexact(endword.."*"))
    end

    -- Sort each group of matches separately, if requested.
    if cwd_first then
        table.sort(first, comparator)
        table.sort(second, comparator)
        match_builder:setnosort(true)
        match_builder:addmatches(first)
        match_builder:addmatches(second)
    end

    return true
end

function exec_generator:generate(line_state, match_builder) -- luacheck: no self
    if line_state:getwordcount() == 1 then
        return exec_matches(line_state, match_builder)
    end
end

-- So that argmatcher :chaincommand() can use exec completion.
internal._exec_matches = exec_matches
