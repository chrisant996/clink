local p = clink.promptfilter(-100) -- Negative number so it runs early.
function p:filter(prompt)
    -- The :filter() function must be defined.  But if the prompt filter is
    -- only interested in surrounding the prompt with escape codes, then the
    -- :filter() function may do nothing.
end
function p:surround()
    -- For the normal (left side) prompt.
    local prompt_prefix  = ""   -- Fill this in with escape codes to be printed before the prompt text.
    local prompt_suffix  = ""   -- Fill this in with escape codes to be printed after the prompt text.
    -- For the right side prompt (if any).
    local rprompt_prefix = ""   -- Fill this in with escape codes to be printed before the right side prompt text.
    local rprompt_suffix = ""   -- Fill this in with escape codes to be printed after the right side prompt text.
    return prompt_prefix, prompt_suffix, rprompt_prefix, rprompt_suffix
end
