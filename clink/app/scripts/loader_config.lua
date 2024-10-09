-- Copyright (c) 2024 Christopher Antos
-- License: http://opensource.org/licenses/MIT

local norm = "\x1b[m"
--local bold = "\x1b[1m"
--local italic = "\x1b[3m"
local underline = "\x1b[4m"
--local reverse = "\x1b[7m"
--local noreverse = "\x1b[27m"

--------------------------------------------------------------------------------
local function printerror(message)
    io.stderr:write(message)
    io.stderr:write("\n")
end

--------------------------------------------------------------------------------
local demo_colors =
{
    ["0"] = "",

    ["A"] = "color.arg",
    ["C"] = "color.cmd",
    ["D"] = "color.doskey",
    ["E"] = "color.executable",
    ["F"] = "color.flag",
    ["G"] = "color.message",
    ["H"] = "color.histexpand",
    ["I"] = "color.input",
    ["L"] = "color.selection",
    ["M"] = "color.argmatcher",
    ["P"] = "color.prompt",
    ["R"] = "color.comment_row",
    ["S"] = "color.suggestion",
    ["U"] = "color.unexpected",
    ["Z"] = "color.unrecognized",

    ["&"] = "color.cmdsep",
    [">"] = "color.cmdredir",
    ["?"] = "color.interact",

    ["d"] = "color.description",
    ["h"] = "hidden",
    ["i"] = "color.arginfo",
    ["l"] = "color.selected_completion",
    ["p"] = "color.common_match_prefix",
    ["r"] = "readonly",

    ["_"] = "cursor",
    ["\\"] = "dir",

    --color.filtered
    --color.horizscroll
    --color.modmark
    --color.popup
    --color.popup_desc
}

local demo_strings =
{
    "!I",
    "{PC:\\>}{Mclink} {Aset} {F--help}",
    "{PC:\\>}{Cecho} hello {>>file} {&&} {Enotepad} file",
    "{PC:\\>}{Dmyalias} This{_▂}{Sis an auto-suggestion}",
    "{PC:\\>}{Zxyzzy.exe} {&&} {Crem} {dAn unrecognized command}",
    "{PC:\\repo>}{Etype} {Lselected.txt}{_▂}",
    "!0",
    --"{h.git\\}  {\\src\\}  file.txt  {hhidden.txt}  {rreadonly.txt}  {lselected.txt}",
    "{\\src\\}  file.txt  {rreadonly.txt}  {lselected.txt}",
}

local function get_from_ini_or_settings(name, ini)
    return ini and ini[name] or settings.get(name) or ""
end

local function sgr(code)
    return "\x1b["..(code or "").."m"
end

local function insert_sgr(code, prefix)
    if prefix then
        local orig = code
        code = code:match("^\x1b%[(.*)m$") or orig
        local hadesc = (orig ~= code)
        code = code:gsub("^0;", "")
        code = prefix..code
        code = code:gsub(";+$", "")
        if hadesc then
            code = sgr(code)
        end
    end
    return code
end

local function get_settings_color(name, ini, zero)
    local color = get_from_ini_or_settings(name, ini)
    if color == "" then
        if name == "color.argmatcher" then
            color = get_from_ini_or_settings("color.executable", ini)
        elseif name == "color.popup" then
            local t = clink.getpopuplistcolors()
            color = t.items
        elseif name == "color.popup_desc" then
            local t = clink.getpopuplistcolors()
            color = t.desc
        elseif name == "color.selected_completion" then
            color = "0;1;7"
        elseif name == "color.selection" then
            color = get_from_ini_or_settings("color.input", ini)
            if color == "" then
                color = "0"
            end
            color = color..";7"
        elseif name == "color.suggestion" then
            color = "0;90"
        end
    end
    color = insert_sgr(color, zero)
    if color:byte(1) ~= 27 then
        color = sgr(color)
    end
    return color
end

local function get_demo_color(c, ini, zero)
    local color
    local name = demo_colors[c]
    if name then
        if name == "cursor" then
            color = zero.."1"
        elseif name == "dir" then
            color = insert_sgr(rl.getmatchcolor("foo", name), zero)
        elseif name == "hidden" or name == "readonly" then
            color = insert_sgr(rl.getmatchcolor("foo", "file,"..name) or "", zero)
        else
            color = get_settings_color(name, ini, zero)
        end
    end
    color = color or zero:gsub(";+$", "")
    if color:byte(1) ~= 27 then
        color = sgr(color)
    end
    return color
end

local function demo_print(s, base_color, zero)
    local t = {}
    local i = 1
    local n = #s
    table.insert(t, insert_sgr(base_color, zero))
    while i <= n do
        local c = s:sub(i, i)
        if c == "{" then
            i = i + 1
            c = s:sub(i, i)
            table.insert(t, get_demo_color(c, nil, zero))
        elseif c == "}" then
            table.insert(t, insert_sgr(base_color, zero))
        else
            table.insert(t, c)
        end
        i = i + 1
    end
    table.insert(t, "\x1b[K")
    table.insert(t, insert_sgr(norm, zero))
    clink.print(table.concat(t))
end

local function make_zero(preferred, use_preferred)
    local zero = "0;"
    if preferred and use_preferred then
        if preferred.background then
            zero = zero..(settings.parsecolor("on "..preferred.background):gsub("^0;", ""))..";"
        end
        if preferred.foreground then
            zero = zero..(settings.parsecolor(preferred.foreground):gsub("^0;", ""))..";"
        end
    end
    return zero
end

local function show_demo(title, preferred, use_preferred)
    local zero = make_zero(preferred, use_preferred)

    if title then
        local pref = ""
        if preferred and preferred.background then
            if use_preferred then
                pref = "  (shown using the preferred background color)"
            else
                local c = settings.parsecolor("on "..preferred.background)
                pref = "  (preferred background: "..sgr(c).."[      ]"..norm..")"
            end
        end
        clink.print(norm..underline..title..norm..pref)
    end

    local base_color = norm
    for _,s in ipairs(demo_strings) do
        if s:sub(1, 1) == "!" then
            base_color = get_demo_color(s:sub(2, 2), nil, zero)
        else
            demo_print(s, base_color, zero)
        end
    end
end

--------------------------------------------------------------------------------
local function list_color_themes(args)
    local fullnames
    local preferred
    local samples
    for i = 1, #args do
        local arg = args[i]
        if arg == "-f" or arg == "--full" then
            fullnames = true
        elseif arg == "-p" or arg == "--preferred" then
            preferred = true
        elseif arg == "-s" or arg == "--samples" then
            samples = true
        elseif arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config theme list")
            print()
            print("  This lists the installed color themes.")
            print()
            print("  Color themes can be found in a themes\\ subdirectory under each directory")
            print("  listed in the \"scripts\" section of the output from running 'clink info'.")
            print()
            print("Options:")
            print("  -f, --full        Show the full path name for each theme.")
            print("  -p, --preferred   Simulate the preferred terminal colors for each theme.")
            print("  -s, --samples     Show color samples from each theme.")
            print("  -h, --help        Show this help text.")
            return true
        end
    end

    local sample_colors =
    {
        {"color.input", "In"},
        {"color.selection", "Se"},
        {"color.argmatcher", "Am"},
        {"color.cmd", "Cm"},
        {"color.doskey", "Do"},
        {"color.arg", "Ar"},
        {"color.flag", "Fl"},
        {"color.arginfo", "Ai"},
        {"color.description", "De"},
        {"color.executable", "Ex"},
        {"color.unrecognized", "Un"},
        {"color.suggestion", "Su"},
    }

    local use_preferred = preferred
    local names, indexed = clink.getthemes()
    if names then
        local maxlen
        if samples then
            maxlen = 1
            for _,n in ipairs(names) do
                if fullnames then
                    n = indexed[clink.lower(n)] or n
                end
                maxlen = math.max(maxlen, console.cellcount(n))
            end
        end

        for _,n in ipairs(names) do
            if fullnames then
                n = indexed[clink.lower(n)] or n
            end

            local s = {}
            table.insert(s, n)
            if samples then
                local ini = clink.readtheme(indexed[clink.lower(n)])
                if ini then
                    local has = {}
                    for _,e in ipairs(ini) do
                        has[e.name] = true
                    end

                    local zero = make_zero(ini.preferred, use_preferred)
                    local justzero = zero:gsub(";+$", "")

                    table.insert(s, string.rep(" ", maxlen + 4 - console.cellcount(n)))
                    for _,e in ipairs(sample_colors) do
                        if has[e[1]] then
                            table.insert(s, get_settings_color(e[1], ini, zero))
                            table.insert(s, e[2])
                            table.insert(s, sgr(justzero))
                        else
                            table.insert(s, sgr(justzero).."  ")
                        end
                    end
                    table.insert(s, norm)

                    if not use_preferred and ini.preferred and ini.preferred.background then
                        local pref = make_zero(ini.preferred, true)
                        if pref and pref ~= "0;" then
                            local color = sgr(pref:gsub(";+$", ""))
                            table.insert(s, "    (preferred background: "..color.."[      ]"..norm..")")
                        end
                    end
                end
            end

            clink.print(table.concat(s))
        end
    end
    return true
end

--------------------------------------------------------------------------------
local function write_color_theme(o, all, rules)
    o:write("[set]\n")

    local list = settings.list()
    for _,entry in ipairs(list) do
        if entry.match:find("^color%.") then
            if all or not entry.source then
                o:write(string.format("%s=%s\n", entry.match, settings.get(entry.match, true) or ""))
            end
        elseif rules and entry.match == "match.coloring_rules" then
            o:write(string.format("%s=%s\n", entry.match, settings.get(entry.match) or ""))
        end
    end
end

local function save_color_theme(args, silent)
    local file
    local yes
    local all
    local rules
    if not args or not args[1] then
        args = {""}
    end
    for i = 1, #args do
        local arg = args[i]
        if arg == "-a" or arg == "--all" then
            all = true
        elseif arg == "-r" or arg == "--rules" then
            rules = true
        elseif arg == "-y" or arg == "--yes" then
            yes = true
        elseif not arg or arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config theme save <name>")
            print()
            print("  This saves the current theme settings into the named theme.")
            print()
            print("  If the <name> includes a file path, then the theme is saved there.  If it")
            print("  doesn't, then the theme is saved in the \"themes\" subdirectory under the")
            print("  current Clink profile directory.")
            print()
            print("Options:")
            print("  -a, --all         Save all color settings, even colors added by Lua scripts.")
            print("  -r, --rules       Also save match coloring rules.")
            print("  -y, --yes         Allow overwriting an existing file.")
            print("  -h, --help        Show this help text.")
            return true
        elseif not file then
            file = arg
        end
    end

    if not file then
        if not silent then
            printerror("No output file specified.")
        end
        return
    end

    file = file:gsub('"', '')

    local ext = path.getextension(file)
    if not ext or ext:lower() ~= ".clinktheme" then
        file = file..".clinktheme"
    end

    local dir = path.getdirectory(file)
    if not dir or dir == "" then
        local profile = os.getenv("=clink.profile")
        if profile and profile ~= "" then
            local themes_dir = path.join(profile, "themes")
            os.mkdir(themes_dir)
            file = path.join(themes_dir, file)
        end
    end

    if not yes and os.isfile(file) then
        if not silent then
            printerror("File '"..file.."' already exists.")
            printerror("Add '--yes' flag to overwrite the file.")
        end
        return
    end

    local o = io.open(file, "w")
    if not o then
        if not silent then
            printerror("Unable to open '"..file.."' for write.")
        end
        return
    end

    write_color_theme(o, all, rules)

    o:close()
    return true
end

--------------------------------------------------------------------------------
local function use_color_theme(args)
    local file
    local clearall
    local nosave
    if not args or (type(args) == "table" and not args[1]) then
        args = {""}
    end
    if type(args) == "table" then
        for i = 1, #args do
            local arg = args[i]
            if arg == "-c" or arg == "--clear-all" then
                clearall = true
            elseif arg == "-n" or arg == "--no-save" then
                nosave = true
            elseif not arg or arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
                print("Usage:  clink config theme use <name>")
                print()
                print("  This loads the named theme and updates the current Clink profile's settings.")
                print("  The loaded theme applies to all Clink sessions using the current Clink")
                print("  profile directory.")
                print()
                print("  If the <name> includes a file path, then the theme is loaded from there.  If")
                print("  it doesn't, then \"themes\" subdirectories are searched to find the named")
                print("  theme.")
                print()
                print("  The current theme is first saved to a theme named 'Previous Theme' in the")
                print("  \"themes\" subdirectory under the current Clink profile directory.")
                print()
                print("  The built-in color settings are cleared back to their default values before")
                print("  applying the theme.  The -c flag also clears any color settings added by Lua")
                print("  scripts.")
                print()
                print("Options:")
                print("  -c, --clear-all   Clear all color settings before using the named theme.")
                print("  -n, --no-save     Don't save the current theme first.")
                print("  -h, --help        Show this help text.")
                return true
            elseif not file then
                file = arg
            end
        end
    else
        file = args
    end
    args = nil -- luacheck: no unused

    file = file:gsub('"', '')

    local fullname = clink.getthemes(file)
    if not fullname then
        printerror("Theme '"..file.."' not found.")
        return
    end
    file = fullname

    if not nosave and not save_color_theme({"--all", "--rules", "--yes", "Previous Theme"}, true--[[silent]]) then
        printerror("Unable to save current theme as 'Previous Theme'.")
        printerror("Add '--no-save' flag to load a theme without saving the current theme.")
        return
    end

    local ini, message = clink.applytheme(file, clearall) -- luacheck: no unused
    if message then
        printerror(message)
        return
    end
    return true
end

--------------------------------------------------------------------------------
local function show_color_theme(args)
    local name
    local onlynamed
    local preferred
    for i = 1, #args do
        local arg = args[i]
        if arg == "-n" or arg == "--only-named" then
            onlynamed = true
        elseif arg == "-p" or arg == "--preferred" then
            preferred = true
        elseif arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config theme show [<name>]")
            print()
            print("  This shows a sample of what the named theme looks like.  If no name is")
            print("  provided, it shows a sample of what the current theme looks like.")
            print()
            print("Options:")
            print("  -n, --only-named  Show only the named theme (don't compare with current).")
            print("  -p, --preferred   Simulate the preferred terminal colors.")
            print("  -h, --help        Show this help text.")
            return true
        elseif not name then
            name = arg
        end
    end

    local file
    if name then
        name = name:gsub('"', '')
        file = clink.getthemes(name)
        if not file then
            printerror("Theme '"..name.."' not found.")
            return
        end
    end

    if file then
        local ini, message = clink.readtheme(file)
        if not ini then
            if message then
                print(message)
            end
            return
        end

        if not onlynamed then
            show_demo("Current Theme")
            print()
        end

        -- Must temporarily load the theme in order for rl.getmatchcolor() to
        -- represent colors properly.
        clink._add_clear_colors(ini)
        settings._overlay(ini, true--[[in_memory_only]])
        show_demo(path.getbasename(file) or name, ini.preferred, preferred)

        -- Skip reloading for performance, since this is running in the
        -- standalone exe.
        --settings.load()
    else
        show_demo()
    end
    return true
end

--------------------------------------------------------------------------------
local function print_color_theme(args)
    local file
    local all
    local nosamples
    for i = 1, #args do
        local arg = args[i]
        if arg == "-a" or arg == "--all" then
            all = true
        elseif arg == "-n" or arg == "--no-samples" then
            nosamples = true
        elseif arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config theme print [<name>]")
            print()
            print("  This prints a list of the settings in the named theme.  If no theme name is")
            print("  provided, it prints a list of the current theme settings.")
            print()
            print("Options:")
            print("  -a, --all         Print all colors from current theme, even colors added")
            print("                    by Lua scripts.")
            print("  -n, --no-samples  Don't print color samples.")
            print("  -h, --help        Show this help text.")
            return true
        else
            file = arg
        end
    end

    local ini, message
    if file then
        file = file:gsub('"', '')
        ini, message = clink.readtheme(file)
        if not ini then
            if message then
                printerror(message)
            end
            return
        end
    else
        ini = {}
        for _,e in ipairs(settings.list()) do
            if e.match:find("^color%.") then
                if all or not e.source then
                    table.insert(ini, {name=e.match, value=settings.get(e.match, true, false)})
                end
            elseif e.match == "match.coloring_rules" then
                table.insert(ini, {name=e.match, value=settings.get(e.match)})
            end
        end
    end
    clink._add_clear_colors(ini, all)

    local anyset
    local anyclear
    local maxlen = 1
    for _,t in ipairs(ini) do
        maxlen = math.max(maxlen, console.cellcount(t.name))
        if t.value then
            anyset = true
        elseif not t.value then
            anyclear = true
        end
    end

    if anyset then
        print("[set]")
        for _, t in ipairs(ini) do
            if t.value then
                local s = {}
                table.insert(s, t.name)
                if not nosamples then
                    table.insert(s, string.rep(" ", maxlen + 4 - console.cellcount(t.name)))
                    if t.name:find("^color%.") then
                        local color = get_settings_color(t.name, ini)
                        table.insert(s, color)
                        table.insert(s, "Sample")
                        table.insert(s, norm)
                    else
                        table.insert(s, "      ")
                    end
                    table.insert(s, "  ")
                end
                table.insert(s, "=")
                if not nosamples then
                    table.insert(s, "  ")
                end
                table.insert(s, t.value)
                clink.print(table.concat(s))
            end
        end
    end

    if anyclear then
        print("[clear]")
        for _, t in ipairs(ini) do
            if not t.value then
                print(t.name)
            end
        end
    end

    if message then
        print()
        printerror(message)
        return
    end
    return true
end

--------------------------------------------------------------------------------
local function clear_color_theme(args)
    local all
    local rules
    for i = 1, #args do
        local arg = args[i]
        if arg == "-a" or arg == "--all" then
            all = true
        elseif arg == "-r" or arg == "--rules" then
            rules = true
        elseif arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config theme clear")
            print()
            print("  This resets color settings to their default values.")
            print()
            print("Options:")
            print("  -a, --all         Clear all color settings, even colors added by Lua scripts.")
            print("  -r, --rules       Also clear match coloring rules.")
            print("  -h, --help        Show this help text.")
            return true
        end
    end

    clink._clear_colors(all, rules)
    return true
end

--------------------------------------------------------------------------------
local function list_custom_prompts(args)
    local fullnames
    for i = 1, #args do
        local arg = args[i]
        if arg == "-f" or arg == "--full" then
            fullnames = true
        elseif arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config prompt list")
            print()
            print("  This lists the installed custom prompts.")
            print()
            print("  Custom prompts can be found in a themes\\ subdirectory under each directory")
            print("  listed in the \"scripts\" section of the output from running 'clink info'.")
            print()
            print("Options:")
            print("  -f, --full        Show the full path name for each custom prompt.")
            print("  -h, --help        Show this help text.")
            return true
        end
    end

    local names, indexed = clink.getprompts()
    if names then
        for _,n in ipairs(names) do
            if fullnames then
                n = indexed[clink.lower(n)] or n
            end
            print(n)
        end
    end
    return true
end

--------------------------------------------------------------------------------
local function use_custom_prompt(args)
    local file
    if not args or (type(args) == "table" and not args[1]) then
        args = {""}
    end
    if type(args) == "table" then
        for i = 1, #args do
            local arg = args[i]
            if not arg or arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
                print("Usage:  clink config prompt use <name>")
                print()
                print("  This loads the named custom prompt and updates the current Clink profile's")
                print("  settings.  The loaded prompt applies to all Clink sessions using the current")
                print("  Clink profile directory.")
                print()
                print("  If the <name> includes a file path, then the custom prompt is loaded from")
                print("  there.  If it doesn't, then \"themes\" subdirectories are searched to find")
                print("  the named custom prompt.")
                print()
                print("Options:")
                print("  -h, --help        Show this help text.")
                return true
            elseif not file then
                file = arg
            end
        end
    else
        file = args
    end
    args = nil -- luacheck: no unused

    file = file:gsub('"', '')

    local fullname = clink.getprompts(file)
    if not fullname then
        printerror("Custom prompt '"..file.."' not found.")
        return
    end
    file = fullname

    local ok, err = pcall(function() require(file) end)
    if not ok then
        if err then
            printerror(err)
        end
        printerror("Unable to load custom prompt '"..file.."'.")
        return
    end

    settings.set("clink.customprompt", file)
    return true
end

--------------------------------------------------------------------------------
local function show_custom_prompt(args)
    local name
    local onlynamed
    for i = 1, #args do
        local arg = args[i]
        if arg == "-n" or arg == "--only-named" then
            onlynamed = true
        elseif arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config prompt show [<name>]")
            print()
            print("  This shows a sample of what the named custom prompt looks like.  If no name")
            print("  is provided, it shows a sample of what the current prompt looks like.")
            print()
            print("Options:")
            print("  -n, --only-named  Show only the named prompt (don't compare with current).")
            print("  -h, --help        Show this help text.")
            return true
        elseif not name then
            name = arg
        end
    end

    local file
    if name then
        name = name:gsub('"', '')
        file = clink.getprompts(name)
        if not file then
            printerror("Custom prompt '"..name.."' not found.")
            return
        end
    end

    if file then
        local ret
        local ok, err = pcall(function() ret = require(file) end)
        if not ok or not ret then
            if err then
                printerror(err)
            end
            printerror("Unable to load custom prompt '"..name.."'.")
            return
        end
    end

    if not file or not onlynamed then
        if file then
            clink.print(norm..underline.."Current Prompt"..norm)
        end
        clink._show_prompt_demo(settings.get("clink.customprompt"))
        if file then
            clink.print()
        end
    end

    if file then
        clink.print(norm..underline..name..norm)
        clink._show_prompt_demo(file)
        if name then
            clink.print()
        end
    end
    return true
end

--------------------------------------------------------------------------------
local function clear_custom_prompt(args)
    for i = 1, #args do
        local arg = args[i]
        if arg == "" or arg == "--help" or arg == "-h" or arg == "-?" then
            print("Usage:  clink config prompt clear")
            print()
            print("  This clears the custom prompt.")
            print("")
            print("  This is the same as running 'clink set clink.customprompt clear'.")
            print()
            print("Options:")
            print("  -h, --help        Show this help text.")
            return true
        end
    end

    settings.clear("clink.customprompt")
    return true
end

--------------------------------------------------------------------------------
local function do_prompt_command(args)
    local command = args[1]
    table.remove(args, 1)

    if command == "list" then
        return list_custom_prompts(args)
    elseif command == "use" then
        return use_custom_prompt(args)
    elseif command == "show" then
        return show_custom_prompt(args)
    elseif command == "clear" then
        return clear_custom_prompt(args)
    elseif not command or command == "" or command == "--help" or command == "-h" or command == "-?" then
        print("Usage:  clink config prompt [command]")
        print()
        print("Commands:")
        print("  list              List custom prompts.")
        print("  use <name>        Use a custom prompt.")
        print("  show [<name>]     Show what the prompt looks like.")
        print("  clear             Clear the custom prompt.")
        print()
        print("Options:")
        print("  -h, --help        Show this help text.")
        return true
    else
        printerror("Unrecognized 'clink config prompt "..command.."' command.")
    end
end

--------------------------------------------------------------------------------
local function do_theme_command(args)
    local command = args[1]
    table.remove(args, 1)

    if command == "list" then
        return list_color_themes(args)
    elseif command == "use" then
        return use_color_theme(args)
    elseif command == "save" then
        return save_color_theme(args)
    elseif command == "show" then
        return show_color_theme(args)
    elseif command == "print" then
        return print_color_theme(args)
    elseif command == "clear" then
        return clear_color_theme(args)
    elseif not command or command == "" or command == "--help" or command == "-h" or command == "-?" then
        print("Usage:  clink config theme [command]")
        print()
        print("Commands:")
        print("  list              List color themes.")
        print("  use <name>        Use a color theme.")
        print("  save <name>       Save the current color theme.")
        print("  show [<name>]     Show what the theme looks like.")
        print("  print [<name>]    Print a color theme.")
        print("  clear             Reset to the default colors.")
        print()
        print("Options:")
        print("  -h, --help        Show this help text.")
        return true
    else
        printerror("Unrecognized 'clink config theme "..command.."' command.")
    end
end

--------------------------------------------------------------------------------
-- luacheck: globals config_loader
config_loader = config_loader or {}
function config_loader.do_config(args)
    local command = args[1]
    table.remove(args, 1)

    if command == "prompt" then
        return do_prompt_command(args)
    elseif command == "theme" then
        return do_theme_command(args)
    elseif not command or command == "" or command == "--help" or command == "-h" or command == "-?" then
        print("Usage:  clink config [command]")
        print()
        print("Commands:")
        print("  prompt            Configure the custom prompt for Clink.")
        print("  theme             Configure the color theme for Clink.")
        print()
        print("Options:")
        print("  -h, --help        Show this help text.")
        return true
    else
        printerror("Unrecognized 'clink config "..command.."' command.")
    end
end
