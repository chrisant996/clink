-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink:argmatcher("cd", "chdir", "pushd", "rd", "rmdir", "md", "mkdir"):addarg(
    function (word_index, line_state)
        local word = line_state:getword(word_index)
        local matches = {}
        for _, dir in ipairs(os.globdirs(word.."*")) do
            table.insert(matches, dir)
        end
        return matches
    end
)
