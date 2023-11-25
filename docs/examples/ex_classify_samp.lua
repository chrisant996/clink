-- In this example, the argmatcher matches a directory as the first argument and
-- a file as the second argument.  It uses a word classifier function to classify
-- directories (words that end with a path separator) as "unexpected" in the
-- second argument position.

local function classify_handler(arg_index, word, word_index, line_state, classifications, user_data)
    -- `arg_index` is the argument position in the argmatcher.
    -- In this example only position 2 needs special treatent.
    if arg_index ~= 2 then
        return
    end

    -- `arg_index` is the argument position in the argmatcher.
    -- `word_index` is the word position in the `line_state`.
    -- Ex1: in `samp dir file` for the word `dir` the argument index is 1 and
    -- the word index is 2.
    -- Ex2: in `samp --help dir file` for the word `dir` the argument index is
    -- still 1, but the word index is 3.

    -- `word` is the word the classifier function was called for and `word_index`
    -- is its position in the line.  Because `line_state` is also provided, the
    -- function can examine any words in the input line.
    if word:sub(-1) == "\\" then
        -- The word appears to be a directory, but this example expects only
        -- files in argument position 2.  Here the word gets classified as "n"
        -- (unexpected) so it gets colored differently.
        classifications:classifyword(word_index, "n")
    end
end

local matcher = clink.argmatcher("samp")
:addflags("--help")
:addarg({ clink.dirmatches })
:addarg({ clink.filematches })
:setclassifier(classify_handler)
