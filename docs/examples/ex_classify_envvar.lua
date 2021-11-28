-- In this example, a custom classifier applies colors to environment variables
-- in the input line.
local envvar_classifier = clink.classifier(50)
function envvar_classifier:classify(commands)
    -- This example doesn't need to parse words within commands, it just wants
    -- to parse the whole line.
    --
    -- So it can simply use the first command's `classifications` object because
    -- the `classifications:applycolor()` method can apply color anywhere in the
    -- entire input line.
    --
    -- (Note that the `classifications:classifyword()` method can only affect
    -- the words for its corresponding command.)
    if commands[1] then
        local line_state = commands[1].line_state
        local classifications = commands[1].classifications
        local line = line_state:getline()
        local len = #line

        -- Loop through the line looking for environment variables, starting at
        -- the first character of the line.
        local idx = 1
        while (idx <= len) do
            -- Find the next percent sign (idx is walking through the line).
            local pos = line:find('%', idx, true--[[plain]])
            if not pos then
                -- No more?  Then all done.
                break
            end

            -- Find the next percent sign (which would close the possible
            -- environment variable).
            local posend = line:find('%', pos + 1, true--[[plain]])
            if not posend then
                -- No close?  Then all done.
                break
            end

            -- Extract the text between the percent signs and check if there
            -- is an environment variable by that name.
            local name = line:sub(pos + 1, posend - 1)
            if name == '' then
                -- Skip a double percent.  It's an escaped percent sign, not an
                -- environment variable.
                idx = posend + 1
            elseif os.getenv(name) then
                -- Apply a color to the environment variable.
                classifications:applycolor(pos, posend - pos + 1, "95")
                idx = posend + 1
            else
                -- Ignore the percent sign, but continue looking for more.
                idx = idx + 1
            end
        end
    end
end
