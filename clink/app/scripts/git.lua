-- Copyright (c) 2024 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- luacheck: globals git
git = {}



--------------------------------------------------------------------------------
local function get_parent(dir)
    local parent = path.toparent(dir)
    if parent and parent ~= "" and parent ~= dir then
        return parent
    end
end

local function is_absolute(pathname)
    local drive = path.getdrive(pathname)
    if not drive then return false end

    local dir = path.getdirectory(pathname)
    if not dir then return false end

    return dir:find("^[/\\]") and true or false
end

local function join_into_absolute(parent, child)
    -- gitdir can (apparently) be absolute or relative, but everything
    -- downstream wants absolute paths.  Process leading .. and . path
    -- components in child.
    while true do
        if child:find('^%.%.[/\\]') or child == '..' then
            parent = path.toparent(parent)
            child = child:sub(4)
        elseif child:find('^%.[/\\]') or child == '.' then
            child = child:sub(3)
        else
            break
        end
    end

    -- Join the remaining parent and child.
    return path.join(parent, child)
end

-- Function that takes (dir, subdir) and returns "dir\subdir" if the subdir
-- exists, otherwise it returns nil.
local function has_dir(dir, subdir)
    local test = path.join(dir, subdir)
    return os.isdir(test) and test or nil
end

--[[
-- Function that takes (dir, file) and returns "dir\file" if the file exists,
-- otherwise it returns nil.
local function has_file(dir, file)
    local test = path.join(dir, file)
    return os.isfile(test) and test or nil
end
--]]

-- Function that walks up from dir, looking for scan_for in each directory.
-- Starting with dir (or cwd if dir is nil), this invokes scan_func(dir), which
-- can check for a subdir or a file or whatever it wants to check.
-- NOTE:  scan_func(dir) must return nil to keep scanning upwards; any other
-- value (including false) is returned to the caller.
local function scan_upwards(dir, scan_func)
    -- Set default path to current directory.
    if not dir or dir == '.' then dir = os.getcwd() end

    repeat
        -- Call the supplied function.
        local result = table.pack(scan_func(dir))
        if result ~= nil and result[1] ~= nil then return table.unpack(result, 1, result.n) end

        -- Walk up to parent path.
        local parent = get_parent(dir)
        dir = parent
    until not dir
end

-- Loads the specified git config file and returns a table with the parsed
-- content.  The returned table has a <code>:get(section, param)</code>
-- function to make it convenient to look up config parameters.
local function load_ini(fileName)
    -- This function is based on https://github.com/Dynodzzo/Lua_INI_Parser/blob/master/LIP.lua
    -- Which is distributed under the MIT License.
    local file = io.open(fileName, 'r')
    if not file then return nil end

    local data = {}
    function data:get(section, param)
        if not section or not param then return end
        section = self[section]
        if not section then return end
        return section[param] or nil
    end

    local section
    for line in file:lines() do
        local tempSection = line:match('^%[([^%[%]]+)%]$')
        if tempSection then
            section = tonumber(tempSection) and tonumber(tempSection) or tempSection
            data[section] = data[section] or {}
        end

        local param, value = line:match('^%s-([%w|_]+)%s-=%s+(.+)$')
        if param and value ~= nil then
            if tonumber(value) then
                value = tonumber(value)
            elseif value == 'true' then
                value = true
            elseif value == 'false' then
                value = false
            end
            if tonumber(param) then
                param = tonumber(param)
            end
            data[section][param] = value
        end
    end
    file:close()
    return data
end

-- Function that builds a command line for running the specified git command.
function git.makecommand(command, include_stderr)
    if not command or command == "" then return end

    command = "git "..command
    if not include_stderr then
        command = "2>nul "..command
    end
    command = "set GIT_OPTIONAL_LOCKS=0&set GIT_ADVICE=0&"..command
    return command
end



--------------------------------------------------------------------------------
--- -name:  git.isgitdir
--- -ver:   1.7.0
--- -arg:   dir:string
--- -ret:   string, string, string | nil
--- Tests whether <span class="arg">dir</span> is a git repo root, or a
--- workspace dir, or asubmodule dir.
---
--- In a git repo, it returns three strings:
--- <ol>
--- <li>The git dir.
--- <li>The workspace dir.  If not in a workspace, then this matches the git dir.
--- <li>The original input <span class="arg">dir</span>.
--- </ol>
---
--- Or when not in a git repo it returns nil.
---
--- Examples:
--- -show:  -- In a repo c:\repo:
--- -show:  git.isgitdir("c:/repo")
--- -show:  -- Returns:  "c:\repo\.git", "c:\repo\.git", "c:\repo"
--- -show:
--- -show:  -- In a submodule under c:\repo:
--- -show:  git.isgitdir("c:/repo/submodule")
--- -show:  -- Returns:  "c:\repo\.git\modules\submodule", "c:\repo\.git", "c:\repo\submodule"
--- -show:
--- -show:  -- In a worktree under c:\repo:
--- -show:  git.isgitdir("c:/repo/worktree")
--- -show:  -- Returns:  "c:\repo\.git\worktrees\worktree", "c:\repo\worktree\.git", "c:\repo\worktree"
--- -show:
--- -show:  -- In a worktree outside c:\repo:
--- -show:  git.isgitdir("c:/worktree")
--- -show:  -- Returns:  "c:\repo\.git\worktrees\worktree", "c:\worktree\.git", "c:\worktree"
function git.isgitdir(dir)
    local function has_git_file(dir) -- luacheck: ignore 432
        local dotgit = path.join(dir, ".git")
        local gitfile
        if os.isfile(dotgit) then
            gitfile = io.open(dotgit)
        end
        if not gitfile then return end

        local git_dir = (gitfile:read() or ""):match("gitdir: (.*)")
        gitfile:close()

        if git_dir then
            -- gitdir can (apparently) be absolute or relative, so a custom
            -- join routine is needed to build an absolute path regardless.
            local abs_dir = join_into_absolute(dir, git_dir)
            if os.isdir(abs_dir) then
                return abs_dir
            end
        end
    end

    -- Return if it's a git dir.
    local gitdir = has_dir(dir, ".git")
    if gitdir then
        gitdir = path.normalise(gitdir)
        return gitdir, gitdir, dir
    end

    -- Check if it has a .git file.
    gitdir = has_git_file(dir)
    if gitdir then
        -- Check if it has a worktree.
        local gitdir_file = path.join(gitdir, "gitdir")
        local file = io.open(gitdir_file)
        local wks
        if file then
            wks = file:read("*l")
            file:close()
        end
        -- If no worktree, check if submodule inside a repo.
        if not wks then
            wks = scan_upwards(dir, function (x)
                return has_dir(x, ".git")
            end)
            if not wks then
                -- No worktree and not nested inside a repo, so give up!
                return
            end
        end
        gitdir = path.normalise(gitdir)
        wks = path.normalise(wks)
        return gitdir, wks, dir
    end
end

--------------------------------------------------------------------------------
--- -name:  git.getgitdir
--- -ver:   1.7.0
--- -arg:   dir:string
--- -ret:   string, string, string | nil
--- Tests whether <span class="arg">dir</span> is part of a git repo.
---
--- In a git repo, it returns three strings:
--- <ol>
--- <li>The git dir.
--- <li>The workspace dir.  If not in a workspace, then this matches the git dir.
--- <li>The original input <span class="arg">dir</span>.
--- </ol>
---
--- Or when not in a git repo it returns nil.
---
--- See <a href="#git.isgitdir">git.isgitdir()</a> for examples (they return
--- the same strings).
function git.getgitdir(dir)
    return scan_upwards(dir, git.isgitdir)
end

--------------------------------------------------------------------------------
--- -name:  git.getcommondir
--- -ver:   1.7.0
--- -arg:   [start_dir:string]
--- -ret:   string | nil
--- Returns the common git dir for <span class="arg">start_dir</span>, or for
--- the current working directory if <span class="arg">git_dir</span> is
--- omitted.
---
--- When in a worktree, this returns the git dir for the main repo, rather
--- than the git dir of the worktree itself.
function git.getcommondir(start_dir)
    local git_dir = git.getgitdir(start_dir)
    if not git_dir then return end

    local commondirfile = io.open(path.join(git_dir, "commondir"))
    if not commondirfile then return git_dir end

    -- If there's a commondir file, we're in a git worktree
    local commondir = commondirfile:read()
    commondirfile.close()
    return path.normalise(is_absolute(commondir) and commondir or path.join(git_dir, commondir))
end

--------------------------------------------------------------------------------
--- -name:  git.getbranch
--- -ver:   1.7.0
--- -arg:   [git_dir:string]
--- -ret:   string, boolean | nil
--- Returns the current git branch for <span class="arg">git_dir</span>, or
--- for the current working directory if <span class="arg">git_dir</span> is
--- omitted.
---
--- If the workspace has a detached HEAD, then this returns two values:  the
--- short hash of the current HEAD commit, and true.
---
--- If unable to get the branch info, then nil is returned.
--- -show:  local branch, detached = git.getbranch()
function git.getbranch(git_dir)
    git_dir = git_dir or git.getgitdir()
    if not git_dir then return end

    -- If git directory not found then we're probably outside of repo or
    -- something went wrong.  The same is when head_file is nil.
    local head_file = io.open(path.join(git_dir, "HEAD"))
    if not head_file then return end

    local HEAD = head_file:read()
    head_file:close()

    -- If HEAD isn't present, something is wrong.
    if not HEAD then return end

    -- If HEAD matches branch expression, then we're on named branch otherwise
    -- it is a detached commit.
    local branch_name = HEAD:match("ref: refs/heads/(.+)")
    if not branch_name then
        return HEAD:sub(1, 7), true
    end

    return branch_name
end

--------------------------------------------------------------------------------
--- -name:  git.getremote
--- -ver:   1.7.0
--- -arg:   [git_dir:string]
--- -ret:   string | nil
--- Gets the remote for the current branch for
--- <span class="arg">start_dir</span>, or for the current working directory
--- if <span class="arg">git_dir</span> is omitted.
---
--- Returns nil if the current branch cannot be found.
function git.getremote(git_dir)
    git_dir = git_dir or git.getgitdir()
    if not git_dir then return end

    local branch = git.getbranch(git_dir)
    if not branch then return end

    -- Load git config info.
    local config = load_ini(git_dir)
    if not config then return end

    -- For remote and ref resolution algorithm see https://git-scm.com/docs/git-push.
    local remote_to_push = config:get('branch "'..branch..'"', 'remote') or ''
    local remote_ref = config:get('remote "'..remote_to_push..'"', 'push') or config:get('push', 'default')

    local remote = remote_to_push
    if remote_ref then remote = remote..'/'..remote_ref end

    if remote ~= '' then
        return remote
    end
end

--------------------------------------------------------------------------------
--- -name:  git.getconflictstatus
--- -ver:   1.7.0
--- -ret:   boolean
--- Gets the conflict status for the repo or worktree associated with the
--- current working directory.
---
--- Returns false if there are no conflicts, otherwise it returns true.
function git.getconflictstatus()
    local file = io.popen(git.makecommand("diff --name-only --diff-filter=U"))
    if not file then return false end

    local conflict = false
    for _ in file:lines() do -- luacheck: ignore 512
        conflict = true
        break
    end
    file:close()
    return conflict
end

--------------------------------------------------------------------------------
--- -name:  git.getaheadbehind
--- -ver:   1.7.0
--- -ret:   string, string | nil
--- Gets the number of commits ahead/behind from upstream.
---
--- In a git repo, it returns two strings:
--- <ol>
--- <li>A string with the number commits ahead of upstream.
--- <li>A string with the number commits behind upstream.
--- </ol>
---
--- Or when unsuccessful it returns nil.
function git.getaheadbehind()
    local file = io.popen(git.makecommand("rev-list --count --left-right @{upstream}...HEAD"))
    if not file then return end

    local ahead, behind
    for line in file:lines() do
        ahead, behind = string.match(line, "(%d+)[^%d]+(%d+)")
    end
    file:close()

    return ahead or "0", behind or "0"
end

--------------------------------------------------------------------------------
--- -name:  git.getstatus
--- -ver:   1.7.0
--- -arg:   [no_untracked:boolean]
--- -arg:   [include_submodules:boolean]
--- -ret:   table | nil
--- This runs <code>git status</code> to collect status information for the
--- repo or worktree associated with the current working directory.
---
--- If unsuccessful, this returns nil.
---
--- Otherwise it returns a table with the following scheme:
--- -show:  {
--- -show:  &nbsp;   dirty = ...                 -- true if working and/or staged changes, otherwise nil
--- -show:  &nbsp;   ahead = ...                 -- number of commits ahead, otherwise nil
--- -show:  &nbsp;   behind = ...                -- number of commits behind, otherwise nil
--- -show:  &nbsp;   unpublished = ...           -- true if unpublished, otherwise nil
--- -show:  &nbsp;   tracked = ...               -- true if any changes in tracked files, otherwise nil
--- -show:  &nbsp;   untracked = ...             -- true if any untracked files or directories, otherwise nil
--- -show:  &nbsp;   conflict = ...              -- true if any conflicted files, otherwise nil
--- -show:  &nbsp;   working = {                 -- nil if no working changes
--- -show:  &nbsp;       add = ...               -- number of added files
--- -show:  &nbsp;       modify = ...            -- number of modified files
--- -show:  &nbsp;       delete = ...            -- number of deleted files
--- -show:  &nbsp;       conflict = ...          -- number of conflicted files
--- -show:  &nbsp;       untracked = ...         -- number of untracked files or directories
--- -show:  &nbsp;   }
--- -show:  &nbsp;   staged = {                 -- nil if no working changes
--- -show:  &nbsp;       add = ...               -- number of added files
--- -show:  &nbsp;       modify = ...            -- number of modified files
--- -show:  &nbsp;       delete = ...            -- number of deleted files
--- -show:  &nbsp;       rename = ...            -- number of renamed files
--- -show:  &nbsp;   }
--- -show:  }
---
--- Example of checking whether the workspace is clean (no changes):
--- -show:  local status = git.getstatus()
--- -show:  if not status then
--- -show:  &nbsp;   print("failed")
--- -show:  elseif status.dirty then
--- -show:  &nbsp;   print("dirty (has changes)")
--- -show:  else
--- -show:  &nbsp;   print("clean (no changes)")
--- -show:  end
function git.getstatus(no_untracked, include_submodules)
    local flags = ""
    if no_untracked then
        flags = flags .. "-uno "
    end
    if include_submodules then
        flags = flags .. "--ignore-submodules=none "
    end

    local file = io.popen(git.makecommand("status "..flags.." --branch --porcelain"))
    if not file then return end

    local w_add, w_mod, w_del, w_con, w_unt = 0, 0, 0, 0, 0
    local s_add, s_mod, s_del, s_ren = 0, 0, 0, 0
    local unpublished, ahead, behind
    local line

    line = file:read("*l")
    if not line or not line:find("^## ") then return end

    unpublished = not line:find("^## (.+)%.%.%.")
    ahead = tonumber(line:match(" %[ahead (%d)+") or "0")
    behind = tonumber(line:match("behind (%d)+%]") or "0")
    ahead = ahead > 0 and ahead or nil
    behind = behind > 0 and behind or nil

    while true do
        line = file:read("*l")
        if not line then break end

        local kindStaged, kind = string.match(line, "(.)(.) ")

        if kind == "A" then
            w_add = w_add + 1
        elseif kind == "M" or w_kind == "T" then
            w_mod = w_mod + 1
        elseif kind == "D" then
            w_del = w_del + 1
        elseif kind == "U" then
            w_con = w_con + 1
        elseif kind == "?" then
            w_unt = w_unt + 1
        end

        if kindStaged == "A" then
            s_add = s_add + 1
        elseif kindStaged == "M" or kindStaged == "T" then
            s_mod = s_mod + 1
        elseif kindStaged == "D" then
            s_del = s_del + 1
        elseif kindStaged == "R" then
            s_ren = s_ren + 1
        end
    end
    file:close()

    local working
    local staged

    if w_add + w_mod + w_del + w_con + w_unt > 0 then
        working = {}
        working.add = w_add
        working.modify = w_mod
        working.delete = w_del
        working.conflict = w_con
        working.untracked = w_unt
    end

    if s_add + s_mod + s_del + s_ren > 0 then
        staged = {}
        staged.add = s_add
        staged.modify = s_mod
        staged.delete = s_del
        staged.rename = s_ren
    end

    local status = {}
    status.dirty = (working or staged) and true or nil
    status.unpublished = unpublished
    status.ahead = ahead
    status.behind = behind
    status.working = working
    status.staged = staged
    status.tracked = (w_add + w_mod + w_del + w_con > 0) and true or nil
    status.untracked = (w_unt > 0) and true or nil
    status.conflict = (w_con > 0) and true or nil
    return status
end



--[=[
--------------------------------------------------------------------------------
--- -name:  git.loadconfig
--- -ver:   1.7.0
--- -arg:   [git_dir:string | system:boolean]
--- -ret:   table | nil
--- Loads a git config file and returns a table containing the data, or
--- returns nil if unable to load the config file.
---
--- The function accepts either a git dir string or a boolean indicating
--- whether to load the system config file.  If neither is provided then it
--- loads the repo config file.
---
--- The returned table has a <code>:get(section, param)</code> function to
--- make it convenient to look up config parameters.
--- -show:  local config = git.loadconfig()
--- -show:  print(config:get("core", "autocrlf"))
--- -show:  print(config:get("user", "name"))
function git.loadconfig(git_dir)
    local file
    if type(git_dir) == "string" or not git_dir then
        file = path.join(git_dir, "config")
    else
        file = path.join(os.getenv("USERPROFILE"), ".gitconfig")
    end
    return load_ini(file)
end
--]=]
