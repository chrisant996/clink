-- Copyright (c) 2022 Christopher Antos
-- License: http://opensource.org/licenses/MIT

local github_repo = "chrisant996/clink"
local tag_filename = "clink_updater_tag"

--------------------------------------------------------------------------------
local function log_info(message)
    log.info("Clink updater: " .. message)
    return message
end

local function concat_error(specific, general)
    if not general then
        return specific
    elseif not specific then
        return general
    else
        return general:gsub("%.$", "") .. "; " .. specific
    end
end

--------------------------------------------------------------------------------
settings.add("clink.autoupdate", true, "Auto-update the Clink program files", "When enabled, periodically checks for updates for the Clink program files.")
settings.add("clink.update_interval", 5, "Days between update checks", "The Clink autoupdater will wait this many days between update checks.")

--------------------------------------------------------------------------------
local powershell_exe
local checked_prereqs

local function make_file_at_path(root, rhs)
    if root and rhs then
        if root ~= "" and rhs ~= "" then
            local ret = path.join(root, rhs)
            if os.isfile(ret) then
                return ret
            end
        end
    end
end

local function find_prereqs()
    if not checked_prereqs then
        local sysroot = os.getenv("systemroot")
        powershell_exe = make_file_at_path(sysroot, "System32\\WindowsPowerShell\\v1.0\\powershell.exe")
        checked_prereqs = true
    end
    if not powershell_exe then
        return nil, log_info("unable to find PowerShell.")
    else
        return powershell_exe
    end
end

--------------------------------------------------------------------------------
local function get_update_dir()
    local target = os.gettemppath()
    if not target or target == "" then
        return nil, log_info("unable to get temp path.")
    end

    target = path.join(target, "clink\\updater")
    if not os.isdir(target) then
        local ok, err = os.mkdir(target)
        if not ok then
            return nil, log_info("unable to create temp path '" .. target .. "'.")
        end
    end

    return target
end

local function get_exe_path(include_exe)
    local exe_path = os.getalias("clink"):match('^"(.*)"')
    if exe_path and not include_exe then
        exe_path = path.toparent(exe_path)
    end
    if exe_path == "" then
        exe_path = nil
    end
    if not exe_path then
        return nil, log_info("unable to find Clink executable file.")
    end
    return exe_path
end

local function test_protected_dir(filename)
    local dir_vars = { "ProgramFiles", "ProgramFiles(x86)", "ProgramData", "SystemRoot" }
    for _, var in ipairs(dir_vars) do
        local value = os.getenv(var)
        if value and clink.lower(filename:sub(1, #value)) == clink.lower(value) then
            return false, log_info("unable to update Clink in protected directory " .. value .. ".")
        end
    end
    return true
end

local function parse_version_tag(tag)
    local maj, min, pat
    maj, min, pat = tag:match("v(%d+)%.(%d+)%.(%d+)")
    if not maj then
        maj, min = tag:match("v(%d+)%.(%d+)")
    end
    if maj and min then
        return tonumber(maj), tonumber(min), tonumber(pat or 0)
    end
end

--------------------------------------------------------------------------------
local function unzip(zip, out)
    if out and out ~= "" and os.isdir(out) then
        local fmt = [[2>nul ]] .. powershell_exe .. [[ -Command $ProgressPreference='SilentlyContinue' ; Expand-Archive -Force -LiteralPath "%s" -DestinationPath "%s" ; echo $error.count]]
        local cmd = string.format(fmt, zip, out)
        local f = io.popen(cmd)
        if f then
            local result = nil
            if f:read() == "0" then
                result = true
            end
            for _ in f:lines() do
                result = nil
            end
            f:close()
            return result
        end
    else
        return nil, log_info("output directory '" .. tostring(out) .. "' does not exist.")
    end
end

local function has_zip_file()
    local update_dir = get_update_dir()
    if not update_dir then
        return nil
    end

    local latest
    local files = os.globfiles(path.join(update_dir, "*.zip"), true)
    for _, f in ipairs(files) do
        if f.type:find("file") then
            if not latest or f.mtime > latest.mtime then
                latest = f
            end
        end
    end
    if not latest then
        return nil
    end
    return latest.name
end

local function can_check_for_update(force)
    local clink_exe, err = get_exe_path(true--[[include_exe]])
    if not clink_exe then
        return false, err
    end

    local t = os.globfiles(clink_exe, true)
    if not t or not t[1] or not t[1].type then
        err = log_info("could not determine target location.")
        return false, err
    end

    if t[1].type:find("readonly") then
        return false, log_info("cannot update because files are readonly.")
    end

    if os.isfile(path.join(path.getdirectory(clink_exe), path.getbasename(clink_exe) .. ".lib")) then
        return false, log_info("autoupdate is disabled for local build directories.")
    end

    local ok, err = find_prereqs()
    if not ok then
        return false, concat_error(err, log_info("autoupdate requires PowerShell."))
    end

    ok, err = test_protected_dir(clink_exe)
    if not ok then
        return false, err
    end

    local tagfile, err = get_update_dir()
    if not tagfile then
        return false, err
    end
    tagfile = path.join(tagfile, tag_filename)

    if not force then
        local f = io.open(tagfile)
        if not f then
            log_info("updating because there is no record of having updated before.")
            return true
        end

        local local_lastcheck = f:read("*l")
        f:close()

        if local_lastcheck then
            local now = os.time()
            local interval_days = settings.get("clink.update_interval")
            if interval_days < 1 then
                interval_days = 1
            end
            if tonumber(local_lastcheck) + (interval_days * 24*60*60) > now then
                return false, log_info("too soon to check for updates (" .. tonumber(local_lastcheck) .. " vs " .. now .. ").")
            end
        end
    end

    return true
end

local function internal_check_for_update(force)
    local local_tag = "v" .. clink.version_major .. "." .. clink.version_minor .. "." .. clink.version_patch

    -- Use github API to query latest release.
    if force then
        print("Checking latest version...")
    end
    local cloud_tag
    local api = string.format([[2>nul ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; Invoke-WebRequest https://api.github.com/repos/%s/releases/latest | Select-Object -ExpandProperty Content"]], github_repo)
    local f, err = io.popen(api)
    if not f then
        return nil, concat_error(err, log_info("unable to query github api."))
    end
    local latest_zip
    for line in f:lines() do
        local tag = line:match('"tag_name": *"(.-)"')
        local match = line:match('"browser_download_url": *"(.-%.zip)"')
        if not cloud_tag and tag then
            cloud_tag = tag
        end
        if not latest_zip and match then
            latest_zip = match
        end
    end
    f:close()
    if not latest_zip then
        return nil, log_info("unable to find latest release zip file.")
    end

    -- Compare versions.
    local needed = true
    log_info("latest release is " .. cloud_tag .. ".")
    local cmaj, cmin, cpat = parse_version_tag(cloud_tag)
    if clink.version_major > cmaj then
        needed = false
    elseif clink.version_major == cmaj then
        if clink.version_minor > cmin then
            needed = false
        elseif clink.version_minor == cmin then
            if clink.version_patch >= cpat then
                needed = false
            end
        end
    end
    if not needed then
        return nil, log_info("no update available; local version " .. local_tag .. " is not older than latest release " .. cloud_tag .. ".")
    end

    -- Download latest release zip file.
    -- Note:  Because of github redirection, there's no way to verify whether
    -- the file existed.  But if it's not a zip file then the expand will fail,
    -- and that's good enough.
    if force then
        print("Downloading latest release...")
    end
    local f
    local local_zip = get_update_dir()
    if local_zip then
        local_zip = path.join(local_zip, cloud_tag .. ".zip")
        f, err = io.open(local_zip, "w+")
    end
    if not f then
        return nil, concat_error(err, log_info("unable to create update zip file."))
    end
    f:close()
    log_info("downloading " .. latest_zip .. " to " .. local_zip .. ".")
    local cmd = string.format([[2>nul ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; Invoke-WebRequest '%s' -OutFile '%s'"]], latest_zip, local_zip)
    f, err = io.popen(cmd)
    if not f then
        os.remove(local_zip)
        return nil, concat_error(err, log_info("failed to download zip file."))
    end
    for _ in f:lines() do
    end
    f:close()
    return local_zip
end

local function check_for_update(force)
    local update_dir, err = get_update_dir()
    if not update_dir then
        return nil, err
    end

    -- Protect against concurrent updates.
    local guardfile = path.join(update_dir, "updating")
    local g, err = io.sopen(guardfile, "w", "w")
    if not g then
        return nil, log_info("unable to update because another update may be in progress; " .. err)
    end

    -- Attempt to update.
    local ret, err = internal_check_for_update(force)

    -- Dispose of the concurrency protection.
    g:close()
    os.remove(guardfile)
    return ret, err
end

local function update_lastcheck(tagfile)
    local f, err = io.open(tagfile, "w")
    if not f then
        local nope = log_info("unable to record last update time to '" .. tagfile .. "'.")
        return nil, concat_error(nope, err)
    end
    f:write(tostring(os.time()) .. "\n")
    f:close()
    return true
end

local function try_update(force)
    local exe_path, err = get_exe_path()
    if not exe_path then
        return nil, err
    end

    local update_dir, err = get_update_dir()
    if not update_dir then
        return nil, err
    end

    local ok, err = find_prereqs()
    if not ok then
        return nil, concat_error(err, log_info("autoupdate requires Windows 10 or higher."))
    end

    local tagfile = path.join(update_dir, tag_filename)

    -- Download latest zip file, or use zip file that's already been downloaded.
    local cloud_tag
    local zip_file, err = has_zip_file()
    if not zip_file then
        if force then
            cloud_tag, err = can_check_for_update()
            if not cloud_tag then
                return nil, err
            end
        end
        zip_file, err = check_for_update(force)
        if not zip_file then
            update_lastcheck(tagfile)
            return nil, err
        end
    end
    if not cloud_tag then
        cloud_tag = path.getbasename(zip_file)
    end

    -- When forcing (`clink update`), the clink executable is in use, so the
    -- downloaded zip file will be applied the next time Clink is injected.
    if force then
        return true, "update to " .. path.getbasename(zip_file) .. " will happen the next time Clink is injected."
    end

    -- Expand the zip file.
    if force then
        print("Expanding zip file...")
    end
    log_info("expanding " .. zip_file .. " into " .. exe_path .. ".")
    local t = os.globfiles(path.join(update_dir, "*.zip"))
    local ok, err = unzip(zip_file, exe_path)
    for _, z in ipairs(t) do
        os.remove(path.join(update_dir, z))
    end
    if not ok then
        local nope = log_info("failed to unzip the latest release.")
        update_lastcheck(tagfile)
        return nil, concat_error(nope, err)
    end

    -- The update appears to have succeeded (and if it didn't, the next check
    -- for updates will reassess and retry).  Write the last update time.
    ok, err = update_lastcheck(tagfile)
    if not ok then
        return nil, err
    end

    return true, log_info("updated Clink to " .. cloud_tag .. ".")
end

local function try_autoupdate()
    -- This wrapper around try_update() is necessary because all coroutines are
    -- initially resumed with (true), because of an oversight in how Clink
    -- manages coroutines for the clink.promptcoroutine() stuff.
    return try_update(false)
end

--------------------------------------------------------------------------------
local function autoupdate()
    if not settings.get("clink.autoupdate") then
        log_info("clink.autoupdate is false.")
    elseif can_check_for_update() then
        local c = coroutine.create(try_autoupdate)
        clink.runcoroutineuntilcomplete(c)
    end
end

clink.oninject(autoupdate)

--------------------------------------------------------------------------------
function clink.updatenow()

    find_prereqs()

    local api = string.format([[2>nul ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; Invoke-WebRequest https://api.github.com/repos/%s/releases/latest | Select-Object -ExpandProperty Content"]], github_repo)
    local f, err = io.popen(api)
    if not f then
        print("ERROR during POPEN")
        return
    end
    local latest_zip
    for line in f:lines() do
        local tag = line:match('"tag_name": *"(.-)"')
        local match = line:match('"browser_download_url": *"(.-%.zip)"')
        if not cloud_tag and tag then
            cloud_tag = tag
        end
        if not latest_zip and match then
            latest_zip = match
        end
    end
    f:close()
    print("TAG", cloud_tag)
    print("URL", latest_zip)

    local ok, err = can_check_for_update(true)
    if not ok then
        return false, err
    end

    return try_update(true)
end
