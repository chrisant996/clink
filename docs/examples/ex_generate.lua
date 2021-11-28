local git_branch_autocomplete = clink.generator(1)

local function starts_with(str, start)
    return string.sub(str, 1, string.len(start)) == start
end

local function is_checkout_ac(text)
    if starts_with(text, "git checkout") then
        return true
    end
    return false
end

local function get_branches()
    -- Run git command to get branches.
    local handle = io.popen("git branch -a 2>&1")
    local result = handle:read("*a")
    handle:close()
    -- Parse the branches from the output.
    local branches = {}
    if starts_with(result, "fatal") == false then
        for branch in string.gmatch(result, "  %S+") do
            branch = string.gsub(branch, "  ", "")
            if branch ~= "HEAD" then
                table.insert(branches, branch)
            end
        end
    end
    return branches
end

function git_branch_autocomplete:generate(line_state, match_builder)
    -- Check if it's a checkout command.
    if not is_checkout_ac(line_state:getline()) then
        return false
    end
    -- Get branches and add them (does nothing if not in a git repo).
    local matchCount = 0
    for _, branch in ipairs(get_branches()) do
        match_builder:addmatch(branch)
        matchCount = matchCount + 1
    end
    -- If we found branches, then stop other match generators.
    return matchCount > 0
end
