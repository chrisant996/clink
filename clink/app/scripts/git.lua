-- Copyright (c) 2024 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
-- luacheck: globals git
git = {}



--------------------------------------------------------------------------------
local function nilwhenzero(x)
    if x and tonumber(x) > 0 then
        return x
    end
end

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

-- Reads the first line from a file.
local function read_from_file(fileName, as_num)
    local file = io.open(fileName, 'r')
    if not file then return nil end

    local line = file:read()
    file:close()
    if not line or line == "" then return nil end

    if as_num then
        line = tonumber(line)
        if not line or line == 0 then return nil end
    end
    return line
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

local function get_branch_slow(git_dir)
    local flags = ""
    if type(git_dir) == "string" then
        git_dir = git_dir:gsub('"', '')
        flags = '--git-dir "'..git_dir..'" '
    end

    local file
    local branch, detached, commit

    -- Handle the most common case first.
    if not branch then
        file = io.popen(git.makecommand(flags.."branch"))
        if file then
            for line in file:lines() do
                local current = line:match("^%*%s+(.*)")
                if current then
                    detached = current:match("^%(HEAD detached at (.*)%)$")
                    branch = detached or current
                    detached = detached and true or nil
                    break
                end
            end
            file:close()
        end
    end

    -- Handle the cases where "git branch" output is empty, but
    -- "git branch --show-current" shows the branch name (e.g. a new repo).
    if not branch then
        file = io.popen(git.makecommand(flags.."branch --show-current"))
        if file then
            for line in file:lines() do -- luacheck: ignore 512
                branch = line
                break
            end
            file:close()
        end
    end

    return branch, detached
end



--------------------------------------------------------------------------------
--- -name:  git.getsystemname
--- -ver:   1.7.0
--- -arg:   [dir:string]
--- -ret:   string | nil
--- Checks whether <span class="arg">dir</span> is under git source
--- control, and returns <code>"git"</code> if it is.  Otherwise returns nil.
function git.getsystemname(dir)
    if git.getgitdir(dir) then
        return "git"
    end
end

--------------------------------------------------------------------------------
--- -name:  git.makecommand
--- -ver:   1.7.0
--- -arg:   command:string
--- -arg:   [include_stderr:boolean]
--- -ret:   string | nil
--- Returns a command line for running the specified git command.  The command
--- line automatically prepends <code>git</code> to the input
--- <span class="arg">command</span> string, plus disables git's optional
--- locks and advice messages.  Unless <span class="arg">include_stderr</span>
--- is true, it also includes <code>2>nul</code> to hide stderr output.
---
--- If <span class="arg">command</span> is missing or empty, it returns nil.
--- -show:  git.makecommand("rev-parse HEAD")
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
--- -arg:   [dir:string]
--- -ret:   string, string, string | nil
--- Tests whether <span class="arg">dir</span> is a git repo root, or a
--- workspace dir, or a submodule dir.  If <span class="arg">dir</span> is
--- omitted then it assumes the current working directory.
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
    dir = dir or os.getcwd()

    if git._fake then
        local git_dir = path.join(dir, ".git")
        return git_dir, git_dir, dir
    end

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
--- -arg:   [dir:string]
--- -ret:   string, string, string | nil
--- Tests whether <span class="arg">dir</span> is part of a git repo.  This
--- starts from <span class="arg">dir</span> and walks up through the parent
--- directories checking <a href="#git.isgitdir">git.isgitdir()</a> for each
--- directory.  If <span class="arg">dir</span> is omitted then it starts from
--- the current working directory.
---
--- In a git repo, it returns three strings:
--- <ol>
--- <li>The git dir.
--- <li>The workspace dir.  If not in a workspace, then this matches the git dir.
--- <li>The original input <span class="arg">dir</span>.
--- </ol>
---
--- Or when not in a git repo it returns nil.
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
--- -arg:   [fast:boolean]
--- -ret:   string, boolean | nil
--- Returns the current git branch for <span class="arg">git_dir</span>, or
--- for the current working directory if <span class="arg">git_dir</span> is
--- omitted.
---
--- In Clink v1.9.4 and higher the optional <span class="arg">fast</span>
--- argument controls how to get the current branch:
--- <ul>
--- <li>When true this uses the fast method of reading the ".git/HEAD" file.
--- <li>When omitted or false this invokes git.exe to get the current branch.
--- <li>Clink v1.9.3 always behave as though <span class="arg">fast</span> is
--- true.
--- <li>In a git repo with a
--- <a href="https://git-scm.com/docs/reftable">reftable</a>, the fast method
--- is unable to get the current branch, and simply returns ".invalid".
--- </ul>
---
--- If the workspace has a detached HEAD, then this returns two values:  the
--- short hash of the current HEAD commit, and true.
---
--- If unable to get the branch info, then nil is returned.
--- -show:  local branch, detached = git.getbranch(os.getcwd(), true)
---
--- <strong>Note:</strong> In Clink v1.9.4 and higher,
--- <span class="arg">fast</span> can be used to help keep the prompt
--- responsive.  A prompt filter can pass true to try to get the branch name
--- the fast way when available.  If a prompt filter gets ".invalid" then it
--- can choose to instead show something like "Loading..." and use
--- <a href="#asyncpromptfiltering">Asynchronous Prompt Filtering</a> to call
--- <code>git.getbranch()</code> again and pass false for
--- <span class="arg">fast</span> to get accurate current branch info in the
--- background.
function git.getbranch(git_dir, fast)
    if git._fake then return git._fake.branch end

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

    -- If HEAD matches branch expression, then we're on named branch
    -- otherwise it is a detached commit.
    local branch_name = HEAD:match("ref: refs/heads/(.+)")

    if os.getenv("CLINK_DEBUG_GIT_REFTABLE") then
        branch_name = ".invalid"
    end

    local detached
    if branch_name ~= ".invalid" or fast then
        if not branch_name then
            branch_name = HEAD:sub(1, 7)
            detached = true
        end
    else
        branch_name, detached = get_branch_slow(git_dir)
    end

    if detached then
        return branch_name, true
    else
        return branch_name
    end
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
    if git._fake then return git._fake.remote end

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
    if git._fake then return git._fake.status and git._fake.status.untracked end

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
    local ahead, behind

    if git._fake then
        ahead = git._fake.status and git._fake.status.ahead
        behind = git._fake.status and git._fake.status.behind
    else
        local file = io.popen(git.makecommand("rev-list --count --left-right @{upstream}...HEAD"))
        if not file then return end

        for line in file:lines() do
            ahead, behind = string.match(line, "(%d+)[^%d]+(%d+)")
        end
        file:close()
    end

    return ahead or "0", behind or "0"
end

-- luacheck: push
-- luacheck: no max line length
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
--- -show:  &nbsp;   branch = ...                -- branch name, or commit hash if detached
--- -show:  &nbsp;   HEAD = ...                  -- HEAD commit hash, or "(initial)"
--- -show:  &nbsp;   detached = ...              -- true if HEAD is detached, otherwise nil
--- -show:  &nbsp;   upstream = ...              -- upstream name, other nil
--- -show:  &nbsp;   dirty = ...                 -- true if working and/or staged changes, otherwise nil
--- -show:  &nbsp;   ahead = ...                 -- number of commits ahead, otherwise nil
--- -show:  &nbsp;   behind = ...                -- number of commits behind, otherwise nil
--- -show:  &nbsp;   unpublished = ...           -- true if unpublished, otherwise nil
--- -show:  &nbsp;   submodule = ...             -- true if in a submodule, otherwise nil
--- -show:  &nbsp;   onlystaged = ...            -- number of changes only in staged files not in working files, otherwise nil
--- -show:  &nbsp;   tracked = ...               -- number of changes in tracked working files, otherwise nil
--- -show:  &nbsp;   untracked = ...             -- number of untracked files or directories, otherwise nil
--- -show:  &nbsp;   conflict = ...              -- number of conflicted files, otherwise nil
--- -show:  &nbsp;   working = {                 -- nil if no working changes
--- -show:  &nbsp;       add = ...               -- number of added files
--- -show:  &nbsp;       modify = ...            -- number of modified files
--- -show:  &nbsp;       delete = ...            -- number of deleted files
--- -show:  &nbsp;       conflict = ...          -- number of conflicted files
--- -show:  &nbsp;       untracked = ...         -- number of untracked files or directories
--- -show:  &nbsp;   }
--- -show:  &nbsp;   staged = {                  -- nil if no staged changes
--- -show:  &nbsp;       add = ...               -- number of added files
--- -show:  &nbsp;       modify = ...            -- number of modified files
--- -show:  &nbsp;       delete = ...            -- number of deleted files
--- -show:  &nbsp;       rename = ...            -- number of renamed files
--- -show:  &nbsp;   }
--- -show:  &nbsp;   total = {                   -- nil if neither working nor staged
--- -show:  &nbsp;       -- This counts files uniquely; if a file "foo" is deleted in working and
--- -show:  &nbsp;       -- also in staged, it counts as only 1 deleted file.  Etc.
--- -show:  &nbsp;       add = ...               -- total added files
--- -show:  &nbsp;       modify = ...            -- total modified files
--- -show:  &nbsp;       delete = ...            -- total deleted files
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
---
--- <strong>Compatibility Note:</strong> This requires a version of git that
--- supports <code>git status --porcelain=v2</code>.  Porcelain v2 format has
--- existed for a long time, so that isn't expected to be a limitation in
--- practice.
---
--- <fieldset><legend>Warning</legend>
--- This runs slowly.  To keep a custom prompt responsive, run this in a
--- coroutine so that it runs in the background (the prompt will automatically
--- refresh when the background operation finishes).  See
--- <a href="#asyncpromptfiltering">Asynchronous Prompt Filtering</a> for more
--- information.
--- </fieldset>
function git.getstatus(no_untracked, include_submodules)
    if git._fake then return git._fake.status end

    local flags = ""
    if no_untracked then
        flags = flags .. "-uno "
    end
    if include_submodules then
        flags = flags .. "--ignore-submodules=none "
    end

    local git_dir, wks_dir = git.getgitdir()
    if not git_dir then return end

    local submodule
    if git_dir then
        submodule = (git_dir:lower():find(path.join(wks_dir:lower(), "modules\\"), 1, true) == 1)
        submodule = submodule and true or nil
    end

    local file = io.popen(git.makecommand("status "..flags.." --branch --porcelain=v2"))
    if not file then return end

    local w_add, w_mod, w_del, w_con, w_unt = 0, 0, 0, 0, 0
    local s_add, s_mod, s_del, s_ren = 0, 0, 0, 0
    local t_add, t_mod, t_del = 0, 0, 0
    local onlystaged = 0

    local hasheader
    local header = {}

    local _, ismain = coroutine.running()

    local tick = os.clock()
    local processed = 0
    for line in file:lines() do
        if line:find("^# ") then
            local k, v = line:match("^#%sbranch%.([^%s]+)%s(.*)$")
            if k then
                hasheader = true
                header[k] = v
            end
        else
            local mode = string.match(line, "^([uU12?]) ")
            if mode == "?" then
                w_unt = w_unt + 1
            elseif mode == "u" or mode == "U" then
                w_con = w_con + 1
            elseif mode == "1" or mode == "2" then
                local kindStaged, kind = string.match(line, "^(.)(.) ", 3)
                local added, modified, deleted

                local w = true
                if kind == "A" or kind == "C" then
                    w_add = w_add + 1
                    added = true
                elseif kind == "M" or kind == "T" or kind == "R" then
                    w_mod = w_mod + 1
                    modified = true
                elseif kind == "D" then
                    w_del = w_del + 1
                    deleted = true
                else
                    w = false
                end

                if kindStaged == "A" or kindStaged == "C" then
                    s_add = s_add + 1
                    if not w then
                        added = true
                    end
                elseif kindStaged == "M" or kindStaged == "T" then
                    s_mod = s_mod + 1
                    if not w then
                        modified = true
                    end
                elseif kindStaged == "D" then
                    s_del = s_del + 1
                    if not w then
                        deleted = true
                    end
                elseif kindStaged == "R" then
                    s_ren = s_ren + 1
                    if not w then
                        modified = true
                    end
                end

                if added then
                    t_add = t_add + 1
                elseif deleted then
                    t_del = t_del + 1
                elseif modified then
                    t_mod = t_mod + 1
                end

                if kindStaged ~= "." and kind == "." then
                    onlystaged = onlystaged + 1
                end
            end
        end
        if not ismain then
            processed = processed + 1
            if processed > 10 and os.clock() - tick > 0.015 then
                coroutine.yield()
                processed = 0
                tick = os.clock()
            end
        end
    end
    file:close()

    if not hasheader then return end

    local working
    local staged
    local total

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

    if t_add + t_mod + t_del > 0 then
        total = {}
        total.add = t_add
        total.modify = t_mod
        total.delete = t_del
    end

    local status = {}
    local oid = header.oid and header.oid:find("^[0-9a-fA-F]") and header.oid:sub(1, 7) or header.oid
    status.dirty = (working or staged) and true or nil
    status.unpublished = not header.upstream
    status.ahead = nilwhenzero(header.ab and header.ab:match("%+(%d+)"))
    status.behind = nilwhenzero(header.ab and header.ab:match("%-(%d+)"))
    status.detached = (header.head == "(detached)") and true or nil
    status.branch = status.detached and oid or header.head or nil
    status.submodule = submodule
    status.HEAD = header.oid
    status.upstream = header.upstream
    status.working = working
    status.staged = staged
    status.total = total
    status.onlystaged = (onlystaged > 0) and onlystaged or nil
    status.tracked = (w_add + w_mod + w_del + w_con > 0) and (w_add + w_mod + w_del + w_con) or nil
    status.untracked = (w_unt > 0) and w_unt or nil
    status.conflict = (w_con > 0) and w_con or nil
    return status
end
-- luacheck: pop

--------------------------------------------------------------------------------
--- -name:  git.getaction
--- -ver:   1.7.0
--- -ret:   string, number, number | nil
--- This checks for git actions in progress, and returns the action name.  For
--- certain actions it may also return the current step number and the total
--- number of steps.
---
--- If unsuccessful, this returns nil.
---
--- The possible returned actions are:
--- <ul>
--- <li><code>"rebase-i"</code>: a rebase -i is in progress (includes step and total).
--- <li><code>"rebase-m"</code>: a rebase -m is in progress (includes step and total).
--- <li><code>"rebase"</code>: a rebase is in progress (includes step and total).
--- <li><code>"am"</code>: a "git am" is in progress (includes step and total).
--- <li><code>"am/rebase"</code>: a "git am" is in progress (includes step and total).
--- <li><code>"merge"</code>: a merge is in progress.
--- <li><code>"cherry-pick"</code>: a cherry-pick is in progress.
--- <li><code>"revert"</code>: a revert is in progress.
--- <li><code>"bisect"</code>: a bisect is in progress.
--- </ul>
---
--- -show:  local action, step, total = git.getaction()
--- -show:  if action then
--- -show:  &nbsp;   print("current git action is '"..action.."'")
--- -show:  &nbsp;   if step and total then
--- -show:  &nbsp;       print(string.format("... step %d of %d", step, total))
--- -show:  &nbsp;   end
--- -show:  else
--- -show:  &nbsp;   print("no git action currently in progress")
--- -show:  end
function git.getaction()
    if git._fake then
        if not git._fake.action then
            return
        elseif git._fake.action_step and git._fake.action_total then
            return git._fake.action, git._fake.action_step, git._fake.action_total
        else
            return git._fake.action
        end
    end

    local git_dir = git.getgitdir()
    if not git_dir then return end

    if os.isdir(path.join(git_dir, "rebase-merge")) then
        local action
        -- FUTURE?: local b = read_from_file(path.join(git_dir, "rebase-merge/head-name"))
        local step = read_from_file(path.join(git_dir, "rebase-merge/msgnum"), true--[[as_num]])
        local num_steps = read_from_file(path.join(git_dir, "rebase-merge/end"), true--[[as_num]])
        if os.isfile(path.join(git_dir, "rebase-merge/interactive") ) then
            action = "rebase-i"
        else
            action = "rebase-m"
        end
        if step and num_steps then
            return action, step, num_steps
        else
            return action
        end
    end

    if os.isdir(path.join(git_dir, "rebase-apply")) then
        local action
        local step = read_from_file(path.join(git_dir, "rebase-apply/next"), true--[[as_num]])
        local num_steps = read_from_file(path.join(git_dir, "rebase-apply/last"), true--[[as_num]])
        if os.isfile(path.join(git_dir, "rebase-apply/rebasing")) then
            -- FUTURE?: local b = read_from_file(path.join(git_dir, "rebase-apply/head-name"))
            action = "rebase"
        elseif os.isfile(path.join(git_dir, "rebase-apply/applying")) then
            action = "am"
        else
            action = "am/rebase"
        end
        if step and num_steps then
            return action, step, num_steps
        else
            return action
        end
    elseif os.isfile(path.join(git_dir, "MERGE_HEAD")) then
        return "merging"
    elseif os.isfile(path.join(git_dir, "CHERRY_PICK_HEAD")) then
        return "cherry-picking"
    elseif os.isfile(path.join(git_dir, "REVERT_HEAD")) then
        return "reverting"
    elseif os.isfile(path.join(git_dir, "BISECT_LOG")) then
        return "bisecting"
    end
end

--------------------------------------------------------------------------------
--- -name:  git.hasstash
--- -ver:   1.7.0
--- -ret:   boolean | nil
--- Returns whether any stashes exist for the repo or worktree associated with
--- the current working directory.
function git.hasstash()
    if git._fake then return (git._fake.stashes or 0) > 0 end

    local file = io.popen(git.makecommand("rev-parse --verify refs/stash"))
    if not file then return end

    local line = file:read("*l") or ""
    return line ~= ""
end

--------------------------------------------------------------------------------
--- -name:  git.getstashcount
--- -ver:   1.7.0
--- -ret:   number | nil
--- Returns the number of stashes that exist for the repo or worktree
--- associated with the current working directory.
function git.getstashcount()
    if git._fake then return git._fake.stashes or 0 end

    local file = io.popen(git.makecommand("rev-list --walk-reflogs --count refs/stash"))
    if not file then return end

    local line = file:read("*l") or "0"
    return tonumber(line)
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
