-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
function clink.dir_matches(match_word, word_index, line_state)
    local word = line_state:getword(word_index)
    local matches = {}
    local root = path.getdirectory(word) or ""
    for _, d in ipairs(os.globdirs(word.."*")) do
        local dir = path.join(root, d)
-- TODO: PERFORMANCE: globdirs should return whether each file is hidden since
-- it already had that knowledge.
        if os.ishidden(dir) then
            table.insert(matches, { match = dir, type = "dir,hidden" })
        else
            table.insert(matches, { match = dir, type = "dir" })
        end
    end
    return matches
end

--------------------------------------------------------------------------------
clink.argmatcher("cd", "chdir", "pushd", "rd", "rmdir", "md", "mkdir"):addarg(
    clink.dir_matches
)
