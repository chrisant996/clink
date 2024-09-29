-- Colors for the prompt strings.
local cwd_color  = "\x1b[0;1;37;44m"
local symbol_color = "\x1b[0;1;34m"
local date_color = "\x1b[0;36m"
local transient_date_color = "\x1b[0;1;30m"
local normal = "\x1b[m"

-- Create prompt filter.
local pf = clink.promptfilter(10)

-- Customize the normal prompt.
function pf:filter(prompt)
    -- Don't return false yet; let rightfilter have a chance.
    return cwd_color.." "..os.getcwd().." "..symbol_color.." > "..normal
end

-- Customize the normal right side prompt.
function pf:rightfilter(prompt)
    -- If you have want stop further filtering, this is where to add returning false.
    return date_color..os.date()    --, false
end

-- Customize the transient prompt.
function pf:transientfilter(prompt)
    -- Don't return false yet; let transientrightfilter have a chance.
    return symbol_color.."> "..normal
end

-- Customize the transient right side prompt.
function pf:transientrightfilter(prompt)
    -- Returns false to stop filtering.
    return transient_date_color..os.date("%c", os.time()), false
end

-- Show a reminder to turn on the transient prompt, to try out the example.
if settings.get("prompt.transient") == "off" then
    print("Use 'clink set prompt.transient same_dir' to enable the transient prompt.")
end
