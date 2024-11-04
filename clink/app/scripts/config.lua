-- Copyright (c) 2024 Christopher Antos
-- License: http://opensource.org/licenses/MIT

-- luacheck: globals git

--------------------------------------------------------------------------------
local loaded_clinkprompts = {}
local clinkprompt_wrapping_module

local function clinkprompt_loader(module)
    local lower_module = clink.lower(module)
    local ret = loaded_clinkprompts[lower_module]
    if not ret then
        local func, loaderr = loadfile(module)
        if func then
            local old = clinkprompt_wrapping_module
            clinkprompt_wrapping_module = lower_module

            local ok, funcerr = pcall(function() ret = func() end)

            clinkprompt_wrapping_module = old
            ret = ok and (ret or true) or funcerr or false
        else
            ret = loaderr or false
        end
        loaded_clinkprompts[lower_module] = ret
    end
    return ret
end

local function clinkprompt_searcher(module)
    if clink.lower(path.getextension(module)) == ".clinkprompt" and os.isfile(module) then
        return clinkprompt_loader
    end
end

table.insert(package.searchers, 2, clinkprompt_searcher)

-- luacheck: globals require
local orig_require = require
require = function(module)
    if clinkprompt_wrapping_module then
        local old = clinkprompt_wrapping_module

        -- Make sure each require() starts with no wrapping module, so it
        -- doesn't matter whether a required module is loaded first by a
        -- .clinkprompt file or by a normal .lua file.
        clinkprompt_wrapping_module = nil
        -- When loading a .clinkprompt file via require(), then the loader
        -- function clinkprompt_loader() is what sets the wrapping module
        -- after this.

        local rets
        local ok, msg = pcall(function() rets = orig_require(module) end)

        clinkprompt_wrapping_module = old

        if not ok then
            -- Some context (such as the original traceback) is lost when
            -- rethrowing the error, but only if an error occurs while
            -- require() tries to load and run a .clinkprompt script file.
            error(msg, 0)
        end
        return rets
    else
        return orig_require(module)
    end
end

--------------------------------------------------------------------------------
local function add_dirs_from_var(t, var, subdir)
    if var and var ~= "" then
        local dirs = string.explode(var, ";", '"')
        for _,d in ipairs(dirs) do
            d = d:gsub("^%s+", ""):gsub("%s+$", "")
            d = rl.expandtilde(d:gsub('"', ""))
            if subdir then
                d = path.join(d, "themes")
            end
            d = path.getdirectory(path.join(d, "")) -- Makes sure no trailing path separator.
            local key = clink.lower(d)
            if not t[key] then
                t[key] = true
                table.insert(t, d)
            end
        end
        return true
    end
end

--------------------------------------------------------------------------------
local function get_theme_files(name, ext)
    if name then
        if os.isfile(name) then
            if clink.lower(path.getextension(name) or "") == ext then
                return os.getfullpathname(name)
            end
        elseif os.isfile(name..ext) then
            return os.getfullpathname(name..ext)
        end
    end

    name = name and clink.lower(name) or nil

    -- Order matters:  if the same name exists in two directories, the first
    -- one in the scripts path order wins.
    -- NOTE:  The `%=clink.bin%` and `%=clink.profile envvars%` are always
    -- included, even if the `clink.path` setting doesn't include them.
    -- Because the built-in themes are under `%=clink.bin%\themes`, and themes
    -- saved by `clink config theme save` go in `%=clink.profile%\themes`.
    local dirs = {}
    add_dirs_from_var(dirs, os.getenv("CLINK_THEMES_DIR"), false)
    add_dirs_from_var(dirs, os.getenv("=clink.bin"), true)
    add_dirs_from_var(dirs, os.getenv("=clink.profile"), true)
    add_dirs_from_var(dirs, clink._get_scripts_path(), true)

    local list = {}
    local indexed = {}
    for _,dir in ipairs(dirs) do
        local t = os.globfiles(path.join(dir, "*"..ext), true)
        if t then
            for _,entry in ipairs(t) do
                if entry.type:find("file") then
                    local basename = path.getbasename(entry.name)
                    if basename then
                        local fullname = path.join(dir, entry.name)
                        local lowername = clink.lower(basename)
                        if not indexed[lowername] then
                            indexed[lowername] = fullname
                            table.insert(list, basename)
                            if name and name == lowername or name == clink.lower(fullname) then
                                return fullname -- Found.
                            end
                        end
                    end
                end
            end
        end
    end

    if name then
        return -- Not found.
    end

    table.sort(list, string.comparematches)
    return list, indexed -- Separate tables in case a theme is named "1"..ext, for example.
end

--------------------------------------------------------------------------------
--- -name:  clink.getprompts
--- -ver:   1.7.0
--- -arg:   [name:string]
--- -ret:   (see remarks below)
--- This has two different behaviors, depending on whether the optional
--- <span class="arg">name</span> argument is provided:
---
--- If <span class="arg">name</span> is omitted, this returns two tables.
--- The first table is an array of custom prompt names.  The second table is a
--- map of lowercase names to their corresponding file paths.
--- -show:  local names, map = clink.getprompts()
--- -show:  for _, name in ipairs(names) do
--- -show:  &nbsp;   local file = map[clink.lower(name)]
--- -show:  &nbsp;   print(string.format('"%s" -> "%s"', name, file))
--- -show:  end
---
--- If <span class="arg">name</span> is a string, this returns the file path
--- of a matching custom prompt file.  If the ".clinkprompt" extension is not
--- present, it is assumed.  If no matching prompt file is found, nil is
--- returned.
--- -show:  clink.getprompt("My Prompt Name")
--- -show:  -- Could return "My Prompt Name.clinkprompt" if that file exists in the current directory.
--- -show:  -- Could return "c:\myclink\scripts\themes\My Prompt Name.clinkprompt" if that exists.
--- -show:  -- Etc.
---
--- Refer to <a href="#customisingtheprompt">Customizing the Prompt</a> for
--- more information.
function clink.getprompts(name)
    return get_theme_files(name, ".clinkprompt")
end

--------------------------------------------------------------------------------
--- -name:  clink.getthemes
--- -ver:   1.7.0
--- -arg:   [name:string]
--- -ret:   (see remarks below)
--- This has two different behaviors, depending on whether the optional
--- <span class="arg">name</span> argument is provided:
---
--- If <span class="arg">name</span> is omitted, this returns two tables.
--- The first table is an array of theme names.  The second table is a map of
--- lowercase names to their corresponding file paths.
--- -show:  local names, map = clink.getthemes()
--- -show:  for _, name in ipairs(names) do
--- -show:  &nbsp;   local file = map[clink.lower(name)]
--- -show:  &nbsp;   print(string.format('"%s" -> "%s"', name, file))
--- -show:  end
---
--- If <span class="arg">name</span> is a string, this returns the file path
--- of a matching theme file.  If the ".clinktheme" extension is not present,
--- it is assumed.  If no matching theme file is found, nil is returned.
--- -show:  clink.getthemes("My Theme Name")
--- -show:  -- Could return "My Theme Name.clinktheme" if that file exists in the current directory.
--- -show:  -- Could return "c:\myclink\scripts\themes\My Theme Name.clinktheme" if that exists.
--- -show:  -- Etc.
---
--- Refer to <a href="#color-themes">Color Themes</a> for more information.
function clink.getthemes(name)
    return get_theme_files(name, ".clinktheme")
end

--------------------------------------------------------------------------------
--- -name:  clink.readtheme
--- -ver:   1.7.0
--- -arg:   theme:string
--- -ret:   table, string
--- Reads the specified theme and returns a table with its settings.  If
--- unsuccessful, it returns nil followed by a message describing the failure.
---
--- <span class="arg">theme</span> can be a filename or the title of an
--- installed theme.
---
--- Refer to <a href="#color-themes">Color Themes</a> for more information.
function clink.readtheme(theme)
    local fullname = clink.getthemes(theme)
    if not fullname then
        return nil, "Theme '"..theme.."' not found."
    end
    theme = fullname

    local ini = settings._parseini(theme)
    if not ini then
        return nil, "Error reading '"..theme.."'."
    end

    local message
    for _, t in ipairs(ini) do
        if t.name == "match.coloring_rules" then
            if os.getenv("CLINK_MATCH_COLORS") then
                message = "CLINK_MATCH_COLORS overrides the theme's match.coloring_rules."
            end
        elseif not t.name:find("^color.") then
            return nil, "Unexpected setting name '"..t.name.."' in '"..theme.."'."
        end
        ini[t.name] = settings.parsecolor(t.value)  -- Allow indexed lookup.
    end

    return ini, message
end

--------------------------------------------------------------------------------
--- -name:  clink.applytheme
--- -ver:   1.7.0
--- -arg:   theme:string
--- -arg:   [clearall:boolean]
--- -arg:   [no_save:boolean]
--- -ret:   table, message
--- Finds the <span class="arg">theme</span> and tries to apply its setting
--- (which are saved in the current profile settings, which affects all Clink
--- sessions using that profile directory).
---
--- If successful, it returns true.  If unsuccessful, it returns nil followed
--- by a message describing the failure.
---
--- Applying a color theme clears the built-in color settings to their default
--- values before loading the theme.  If the optional
--- <span class="arg">clearall</span> argument is true, then it also clears
--- any color settings added by Lua scripts.
---
--- If the optional <span class="arg">no_save</span> argument is true, then
--- the color theme is loaded into memory but is not saved back to the Clink
--- settings file.
---
--- Refer to <a href="#color-themes">Color Themes</a> for more information.
function clink.applytheme(file, clearall, no_save)
    local ini, message = clink.readtheme(file)
    if not ini then
        return nil, message
    end

    -- FUTURE: what about match coloring rules?
    clink._add_clear_colors(ini, clearall, false)
    settings._overlay(ini, no_save and true or nil)
    return ini
end

--------------------------------------------------------------------------------
function clink._load_colortheme_in_memory(theme)
    if type(theme) == "string" then
        theme = theme:gsub('"', ''):gsub('%s+$', '')
        if theme == "" then
            return
        end
        theme = clink.readtheme(theme)
    end
    if type(theme) == "table" then
        clink._add_clear_colors(theme)
        settings._overlay(theme, true--[[in_memory_only]])
    end
end

--------------------------------------------------------------------------------
function clink._add_clear_colors(ini, all, rules)
    local has = {}
    for _,t in ipairs(ini) do
        if t.name then
            has[t.name] = true
        end
    end
    for _,e in ipairs(settings.list()) do
        if not has[e.match] then
            if e.match:find("^color%.") then
                if all or not e.source then
                    table.insert(ini, {name=e.match})
                end
            elseif rules and e.match == "match.coloring_rules" then
                table.insert(ini, {name=e.match})
            end
        end
    end
end

--------------------------------------------------------------------------------
function clink._clear_colors(all, rules)
    local ini = {}
    clink._add_clear_colors(ini, all, rules)
    settings._overlay(ini)
end

--------------------------------------------------------------------------------
function clink._show_prompt_demo(module)
    git._fake = {
        status = {
            branch = "main",
            HEAD = "a1b2c3d",
            upstream = "origin",
            stashes = 27,
            dirty = true,
            behind = 19,
            working = {
                modify = 3,
                untracked = 1,
            },
            total = {
                modify = 3,
            },
            tracked = 3,
            untracked = 1,
        },
    }

    clink.print("\x1b[m", NONL)

    local m = clink._activate_clinkprompt_module(module)

    if type(m) == "string" then
        print(m)
    elseif type(m) ~= "table" or not m.demo then
        local simulated_cursor = "\x1b[0;7m \x1b[m"
        local left = clink._expand_prompt_codes(os.getenv("PROMPT") or "$p$g")
        local right = clink._expand_prompt_codes(os.getenv("CLINK_RPROMPT") or "", true)
        left, right = clink._filter_prompt(left, right, "", 1)
        left = (left or "")..simulated_cursor
        right = right or ""
        local left_width = console.cellcount(left:gsub("^.*\n", ""))
        local right_width = console.cellcount(right)
        if right_width <= 0 or left_width + right_width + 4 > console.getwidth() then
            right = ""
        else
            right = string.rep(" ", console.getwidth() - left_width - right_width)..right
        end
        clink.print(left..right)
    else
        m.demo()
    end

    clink._activate_clinkprompt_module(nil)

    clink.print("\x1b[m\x1b[K", NONL)

    git._fake = nil
end

--------------------------------------------------------------------------------
function clink._get_clinkprompt_wrapping_module()
    return clinkprompt_wrapping_module
end
