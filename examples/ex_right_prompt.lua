local p = clink.promptfilter(30)
function p:filter(prompt)
    -- The :filter() function must be defined.  But if the prompt filter is
    -- only interested in modifying the right side prompt, then the :filter()
    -- function may do nothing.
end
function p:rightfilter(prompt)
    local sep = #prompt > 0 and "  " or ""
    return os.date()..sep..prompt
end
