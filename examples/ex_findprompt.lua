local max_recent_prompts = 10
local min_match_text = 10

local recent_prompts = {}

local was_top
local scroll_stack_console_height = 0
local scroll_stack = {}

local function add_recent_prompt(text)
	-- Escape characters that have special meaning in regular expressions.
	text = "^"..text:gsub("([!-/:-@[-`{-~])", "\\%1")
	table.insert(recent_prompts, 1, text)
	while #recent_prompts > max_recent_prompts do
		table.remove(recent_prompts)
	end
end

local function reset_prompt_scroll_stack()
	was_top = nil
	scroll_stack = {}
end

local function update_recent_prompt_queue()
	-- Offset minus one because onendedit happens after the cursor moves
	-- down past the end of the input area.
	local offset = -1

	local info = rl.getpromptinfo()
	local line = info.promptline + offset
	local last_line = console.getnumlines()
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

clink.onbeginedit(reset_prompt_scroll_stack)
clink.onendedit(update_recent_prompt_queue)

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
			console.scroll("absolute", scroll_stack[count] - offset)
			was_top = console.gettop()
		end
		return
	end

	local match
	repeat
		match = console.findprevline(start, text, "regex", {})
		if match <= 0 then
			console.scroll("absolute", scroll_stack[count] - offset)
			was_top = console.gettop()
			return
		end
		start = match - 1
	until match - offset < was_top

	table.insert(scroll_stack, match)
	console.scroll("absolute", match - offset)
	was_top = console.gettop()
end

function find_next_prompt(rl_buffer)
	local height = console.getheight()
	local offset = math.modf((height - 1) / 2)

	if #scroll_stack > 0 then
		table.remove(scroll_stack)
	end
	if #scroll_stack == 0 then
		rl_buffer:ding()
		return
	end

	local top = scroll_stack[#scroll_stack] - offset
	if #scroll_stack == 1 then
		top = console.getnumlines()
		table.remove(scroll_stack, #scroll_stack)
	end

	console.scroll("absolute", top)
	was_top = console.gettop()
end
