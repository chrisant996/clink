local max_recent_prompts = 10   -- Keep track of up to this many recent prompts.
local min_match_text = 10       -- Minimum length of text to count as a match.

local recent_prompts = {}       -- Queue of recent prompt match strings.
local scroll_stack = {}         -- Stack of found scroll positions.

-- Remember our most recent scroll position, so that if something else scrolls
-- the screen we can reset the find_prev_prompt/find_next_prompt stack.
local was_top

local function add_recent_prompt(text)
    -- Escape characters that have special meaning in regular expressions.
    text = "^"..text:gsub("([!-/:-@[-`{-~])", "\\%1")

    -- Add new entry at the beginning of the queue.
    table.insert(recent_prompts, 1, text)

    -- Discard excess entries from the end of the queue.
    while #recent_prompts > max_recent_prompts do
        table.remove(recent_prompts)
    end
end

local function reset_prompt_scroll_stack()
    was_top = nil
    scroll_stack = {}
end

local function update_recent_prompt_queue()
    -- Offset minus one because onendedit happens after the cursor moves down
    -- past the end of the input area, which skews the info returned from
    -- rl.getpromptinfo().
    local offset = -1
    local info = rl.getpromptinfo()
    local line = info.promptline + offset
    local last_line = console.getnumlines()

    -- Find a long enough string to be considered part of the prompt.
    while line <= last_line do
        local text = console.getlinetext(line)
        text = text:gsub("%s+$", "")
        if #text >= min_match_text then
            add_recent_prompt(text)
            break
        end
        line = line + 1
    end
end

-- Register for events to maintain the scroll stack and recent prompt queue.
clink.onbeginedit(reset_prompt_scroll_stack)
clink.onendedit(update_recent_prompt_queue)

-- Jumps to the previous prompt on the screen (i.e. move upward, searching for
-- preceding recent prompts).
function find_prev_prompt(rl_buffer)
    local height = console.getheight()
    local offset = math.modf((height - 1) / 2)

    local top = console.gettop()
    if was_top and was_top ~= top then
        reset_prompt_scroll_stack()
    end
    if not was_top then
        was_top = top
    end

    if top <= 1 then
        console.scroll("absolute", 1)
        return
    end

    -- Init the stack if it's empty.
    if #scroll_stack == 0 then
        local info = rl.getpromptinfo()
        scroll_stack[1] = info.promptline
    end

    local count = #scroll_stack
    local start = scroll_stack[count] - 1
    local text = recent_prompts[count]

    if not text then
        if count == 1 then
            -- Only ding if there are none; otherwise visual bell will
            -- scroll back to the bottom.
            rl_buffer:ding()
        else
            -- No more recent prompts?  Maintain the scroll position.
            console.scroll("absolute", scroll_stack[count] - offset)
            was_top = console.gettop()
        end
        return
    end

    -- Search upwards for the next most recent prompt.
    local match
    repeat
        match = console.findprevline(start, text, "regex", {})
        if match <= 0 then
            -- Can't find it?  Maintain the scroll position.
            console.scroll("absolute", scroll_stack[count] - offset)
            was_top = console.gettop()
            return
        end
        start = match - 1
    until match - offset < was_top

    -- Add the found prompt to the stack.
    table.insert(scroll_stack, match)

    -- Scroll to the prompt position
    console.scroll("absolute", match - offset)
    was_top = console.gettop()
end

-- Jump to the next prompt on the screen (i.e. move downward, backtracking over
-- the prompts already visited by find_prev_prompt).
function find_next_prompt(rl_buffer)
    local height = console.getheight()
    local offset = math.modf((height - 1) / 2)

    -- Pop the last found prompt.  If the stack is empty, ding.
    if #scroll_stack > 0 then
        table.remove(scroll_stack)
    end
    if #scroll_stack == 0 then
        rl_buffer:ding()
        return
    end

    -- Get the scroll position of the next to last found prompt.
    local top = scroll_stack[#scroll_stack] - offset
    if #scroll_stack == 1 then
        -- If it's the last prompt, pop it to reset the stack.
        top = console.getnumlines()
        table.remove(scroll_stack, #scroll_stack)
    end

    -- Scroll to the prompt position.
    console.scroll("absolute", top)
    was_top = console.gettop()
end
