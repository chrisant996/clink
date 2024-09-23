-- Copyright (c) 2024 Christopher Antos
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local loaded_clinkprompts = {}
local clinkprompt_wrapping_module

local function clinkprompt_loader(module)
    local ret
    local lower_module = clink.lower(module)
    assert(not loaded_clinkprompts[lower_module])

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
    return ret
end

local function clinkprompt_searcher(module)
    if clink.lower(path.getextension(module)) == ".clinkprompt" and os.isfile(module) then
        return clinkprompt_loader
    end
end

table.insert(package.searchers, 2, clinkprompt_searcher)

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
    local dirs = {}
    local env = os.getenv("CLINK_THEMES_DIR")
    add_dirs_from_var(dirs, env, false)
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
--- TODO: Document how .clinkprompt files work, and link to that section.
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
--- TODO: Document how .clinktheme files work, and link to that section.
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
--- TODO: Document how color themes work, and link to that section.
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
--- -ret:   table, message
--- Finds the <span class="arg">theme</span> and tries to apply its setting
--- (which are saved in the current profile settings, which affects all Clink
--- sessions using that profile directory).
---
--- If successful, it returns true.  If unsuccessful, it returns nil followed
--- by a message describing the failure.
function clink.applytheme(file)
    local ini, message = clink.readtheme(file)
    if not ini then
        return nil, message
    end
    settings._overlay(ini)
    return ini
end

--------------------------------------------------------------------------------
function clink._show_prompt_demo(module)
    local m
    if not module then
        module = clink.getprompts(settings.get("clink.customprompt")) or ""
    end
    pcall(function() m = require(module) end)

    clink.print("\x1b[m", NONL)
    if not m or not m.demo then
        print("Demo not available.")
    else
        m.demo()
        clink.print("\x1b[m\x1b[K", NONL)
    end
end

--------------------------------------------------------------------------------
function clink._get_clinkprompt_wrapping_module()
    return clinkprompt_wrapping_module
end
