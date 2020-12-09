-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
function clink.dir_matches(match_word, word_index, line_state)
    local word = line_state:getword(word_index)
    local expanded
    word, expanded = rl.expandtilde(word)

    local root = path.getdirectory(word) or ""
    if expanded then
        root = rl.collapsetilde(root)
    end

    local matches = {}
    for _, d in ipairs(os.globdirs(word.."*", true)) do
        local dir = path.join(root, d.name)
        table.insert(matches, { match = dir, type = d.type })
    end
    return matches
end

--------------------------------------------------------------------------------
clink.argmatcher("cd", "chdir", "pushd", "rd", "rmdir", "md", "mkdir"):addarg(
    clink.dir_matches
):nofiles()
