-- Copyright (c) 2022 Christopher Antos
-- License: http://opensource.org/licenses/MIT

local github_repo = "chrisant996/clink"
local tag_filename = "clink_updater_tag"

--------------------------------------------------------------------------------
local function log_info(message)
    log.info("Clink updater: " .. message, 2--[[stack level; our caller]])
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
local powershell_exe
local checked_prereqs
local prereq_error

local this_install_type
local this_install_key
local latest_cloud_tag

local is_build_dir = false
local can_use_setup_exe = false -- Always use zip updater, because exe gets blocked sometimes by malware protection.
local dont_check_for_update
local need_lf

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

local function is_rhs_version_newer(lhs, rhs)
    local lmaj, lmin, lpat = parse_version_tag(lhs or "")
    local rmaj, rmin, rpat = parse_version_tag(rhs or "")

    if not lmaj then
        return rmaj and true
    end
    if not rmaj then
        return false
    end

    if rmaj > lmaj then
        return true
    elseif rmaj < lmaj then
        return false
    end

    if rmin > lmin then
        return true
    elseif rmin < lmin then
        return false
    end

    if rpat > lpat then
        return true
    else
        return false
    end
end

local function make_file_at_path(root, rhs)
    if root and rhs then
        if root ~= "" and rhs ~= "" then
            local ret = path.join(root, rhs)
            if os.isfile(ret) then
                return '"' .. ret .. '"'
            end
        end
    end
end

local function find_prereqs()
    if not checked_prereqs then
        local sysroot = os.getenv("systemroot")
        powershell_exe = make_file_at_path(sysroot, "System32\\WindowsPowerShell\\v1.0\\powershell.exe")
        checked_prereqs = true

        if not powershell_exe then
            prereq_error = log_info("unable to find PowerShell v5.")
        else
            -- clink.execute() launches powershell without a console window,
            -- which prevents PowerShell v4.0 on Windows 8.1 from altering the
            -- host's window title.
            local o = clink.execute('2>&1 ' .. powershell_exe .. ' -Command "Get-Host | Select-Object Version"')
            if type(o) ~= "table" then
                powershell_exe = nil
            else
                for _,line in ipairs(o) do
                    local ver = line:match("^ *([0-9]+%.[0-9]+)")
                    if not prereq_error and ver then
                        if is_rhs_version_newer("v" .. ver, "v5.0") then
                            powershell_exe = nil
                            prereq_error = log_info("found PowerShell v" .. ver .. ".")
                        end
                    end
                end
            end
        end
    end

    if prereq_error then
        return nil, prereq_error
    end

    return true
end

--------------------------------------------------------------------------------
local function get_update_dir()
    local target = os.gettemppath()
    if not target or target == "" then
        return nil, log_info("unable to get temp path.")
    end

    target = path.join(target, "clink\\updater")
    if not os.isdir(target) then
        local ok, err = os.mkdir(target) -- luacheck: no unused
        if not ok then
            return nil, log_info("unable to create temp path '" .. target .. "'.")
        end
    end

    return target
end

local function get_bin_dir()
    local bin_dir = os.getenv("=clink.bin")
    if bin_dir == "" then
        bin_dir = nil
    end
    if not bin_dir then
        return nil, log_info("unable to find Clink executable file.")
    end
    return bin_dir
end

local function get_local_tag()
    return "v" .. clink.version_major .. "." .. clink.version_minor .. "." .. clink.version_patch
end

local function did_themes_update_fail(exe_path, cloud_tag)
    exe_path = exe_path or get_bin_dir()
    if exe_path then
        local expand_dir = get_update_dir()
        local local_tag = get_local_tag()
        if expand_dir and
                os.isdir(path.join(path.join(expand_dir, local_tag), "themes")) and
                not os.isdir(path.join(exe_path, "themes")) then
            if not cloud_tag or local_tag == cloud_tag then
                return true
            end
        end
    end
end

local function get_installation_type()
    if not this_install_type then
        this_install_type, this_install_key = clink._get_installation_type()
    end

    -- This sets this_install_type to the actual installation type.  This
    -- returns "zip" if can_use_setup_exe is false, otherwise it returns the
    -- actual installation type.
    return can_use_setup_exe and this_install_key and this_install_type or "zip"
end

--------------------------------------------------------------------------------
local function collect_output(output, line)
    local num = #output
    if num < 25 then
        table.insert(output, line)
    elseif num == 25 then
        table.insert(output, "...")
    end
end

local function log_output(command, output)
    log_info(command)
    if #output > 0 then
        log_info("output from command:")
        for _,line in ipairs(output) do
            log_info("    "..line)
        end
    else
        log_info("no output from command.")
    end
end

--------------------------------------------------------------------------------
local function delete_files(dir, wild, except)
    if except then
        except = path.join(dir, except)
    end
    local t = os.globfiles(path.join(dir, wild), true)
    for _, d in ipairs(t) do
        if d.type:find("file") then
            local full = path.join(dir, d.name)
            if not except or not string.equalsi(full, except) then
                local ok, msg = os.remove(full)
                if not ok then
                    log_info(msg)
                end
            end
        end
    end
end

local function delete_expand_dir(expand_dir)
    local ok, msg
    local themes_dir = path.join(expand_dir, "themes")
    if os.isdir(themes_dir) then
        delete_files(themes_dir, "*")
        ok, msg = os.rmdir(themes_dir)
        if not ok then
            log_info(msg)
        end
    end
    if os.isdir(expand_dir) then
        delete_files(expand_dir, "*")
        ok, msg = os.rmdir(expand_dir)
        if not ok then
            log_info(msg)
        else
            log_info("successfully removed temp path '" .. expand_dir .. "'.")
        end
    end
end

local function unzip(zip, out)
    if not out or out == "" or not os.isdir(out) then
        return nil, log_info("output directory '" .. tostring(out) .. "' does not exist.")
    end

    local success_tag = "CLINK-UNZIP-SUCCEEDED"
    local failure_tag = "CLINK-UNZIP-FAILED"
    -- Including the module name avoids problems if something overrides the Expand-Archive cmdlet.
    -- For example, older versions of PowerShell Community Extensions did (https://github.com/pscx/pscx).
    local expand_archive = string.format([[$ProgressPreference='SilentlyContinue' ; Microsoft.PowerShell.Archive\Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s']], zip, out) -- luacheck: no max line length
    local powershell_command = string.format([[try { %s ; echo '%s' } catch { echo '%s' ; echo $_.Exception.Message ; echo '' 'ALL ERRORS:' '' ; echo $error }]], expand_archive, success_tag, failure_tag) -- luacheck: no max line length
    local cmd = string.format([[2>&1 %s -Command "%s"]], powershell_exe, powershell_command)
    local f, err = io.popen(cmd)
    if not f then
        log_info(cmd)
        return nil, err
    end

    local result
    local output = {}
    local saw_success
    local saw_failure
    for line in f:lines() do
        local utf8 = unicode.fromcodepage(line)
        line = utf8 or line
        collect_output(output, line)
        if line == success_tag then
            saw_success = true
        elseif line == failure_tag then
            saw_failure = true
        end
    end
    if saw_success and not saw_failure then
        result = true
    end
    f:close()

    if not result then
        log_output(cmd, output)
        -- Delete the zip file; it might have been damaged, and re-downloading
        -- it might be necessary.
        os.remove(zip)
        return nil
    end

    return result
end

local function install_file(from_dir, to_dir, name)
    local src = path.join(from_dir, name)
    local dst = path.join(to_dir, name)

    -- Make sure the destination directory exists.
    if not os.isdir(to_dir) then
        os.mkdir(to_dir)
    end

    -- Copy file.
    local ok, err, code = os.copy(src, dst)
    if ok then
        return true
    end

    -- No?  Create a tmp file to reserve a unique name for renaming.
    local file, err2, code2 = os.createtmpfile("~clink.", ".old", to_dir, "b")
    if not file then
        if code2 == 13 then
            -- Access denied when trying to create tmp file for renaming may
            -- indicate elevation is required.
            code = -1
        end
        return nil, err, code
    end
    file:close()

    -- Delete the temp file so the name is available for renaming.
    local tmp = err2
    os.remove(tmp)

    -- Rename away.
    ok, err2, code2 = os.move(dst, tmp) -- luacheck: no unused
    if not ok then
        os.remove(tmp)
        return nil, err, code
    end

    -- Retry copy.
    ok, err2, code2 = os.copy(src, dst) -- luacheck: no unused
    if ok then
        return true
    end

    -- Still no?  Rename back.
    os.move(tmp, dst)
    return nil, err, code
end

local function has_update_file()
    local update_dir = get_update_dir()
    if not update_dir then
        return nil
    end

    local install_type = get_installation_type()
    if not install_type then
        return nil
    end

    local latest
    local files = os.globfiles(path.join(update_dir, "*." .. install_type), 2)
    for _, f in ipairs(files) do
        if f.size > 0 and f.type:find("file") then
            if is_rhs_version_newer(latest and latest.name, f.name) then
                latest = f
            end
        end
    end
    if not latest then
        return nil
    end
    return path.join(update_dir, latest.name)
end

local function can_check_for_update(force)
    local bin_dir, err = get_bin_dir()
    if not bin_dir then
        return false, err
    end

    local t = os.globfiles(path.join(bin_dir, "clink_x??.exe"), true)
    if not t or not t[1] or not t[1].type then
        err = log_info("could not determine target location.")
        return false, err
    end

    if t[1].type:find("readonly") then
        return false, log_info("cannot update because files are readonly.")
    end

    local lib = path.join(bin_dir, path.getbasename(t[1].name) .. ".lib")
    if os.isfile(lib) then
        local reason = "autoupdate is disabled for local build directories."
        if not is_build_dir then
            log_info(reason)
            is_build_dir = true
        end
        return false, reason
    end

    local tagfile, err = get_update_dir() -- luacheck: ignore 411
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

        if did_themes_update_fail(bin_dir) then
            log_info("updating because the themes directory is missing.")
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
                return false, log_info("too soon to check for updates (" .. tonumber(local_lastcheck) .. " vs " .. now .. ").") -- luacheck: no max line length
            end
        end
    end

    return true
end

local function internal_check_for_update(force)
    local local_tag = get_local_tag()
    local install_type = get_installation_type()

    -- Use github API to query latest release.
    --
    -- PowerShell needs -UseBasicParsing to prevent assuming the response is
    -- HTML and attempting to use IE to parse the DOM and execute scripts, and
    -- and needs to force TLS 1.2 because older versions of PowerShell default
    -- to using TLS 1.0.
    if force then
        need_lf = true
        print("Checking latest version...")
    end
    local api, cloud_tag, mock
    if clink.DEBUG and os.getenv("DEBUG_UPDATE_FILE") then
        mock = os.getenv("DEBUG_UPDATE_FILE")
        local ver = mock:match("clink%.(%d+%.%d+%.?%d*)%.[0-9a-fA-F]+%.zip")
        if not ver then
            error("invalid DEBUG_UPDATE_FILE name '" .. mock .. "'.")
        end
        api = string.format([[2>&1 echo "tag_name": "v%s"& echo "browser_download_url": "%s"]], ver, mock) -- luacheck: no max line length
    else
        api = string.format([[2>&1 ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12 ; Invoke-WebRequest -Headers @{\"cache-control\"=\"no-cache\"} -UseBasicParsing https://api.github.com/repos/%s/releases/latest | Select-Object -ExpandProperty Content"]], github_repo) -- luacheck: no max line length
    end
    -- TODO:  use http.get() API:
    -- local options = { user_agent="Clink-Updater/1.0", no_cache=true }
    -- local data = http.get("https://api.github.com/repos/chrisant996/clink/releases/latest", options)

    local f, err = io.popen(api)
    if not f then
        log_info(api)
        return nil, concat_error(err, log_info("unable to query github api."))
    end
    local latest_update_file
    local output = {}
    for line in f:lines() do
        local utf8 = unicode.fromcodepage(line)
        line = utf8 or line
        local tag = line:match('"tag_name": *"([^"]-)"')
        local match = line:match('"browser_download_url": *"([^"]-%.' .. install_type .. ')"')
        if not cloud_tag and tag then
            cloud_tag = tag
        end
        if not latest_update_file and match then
            latest_update_file = match
        end
        collect_output(output, line)
    end
    f:close()
    if not cloud_tag then
        log_output(api, output)
        return nil, log_info("unable to find latest release.")
    elseif not latest_update_file then
        log_output(api, output)
        return nil, log_info("unable to find latest release " .. install_type .. " file.")
    end
    latest_cloud_tag = cloud_tag

    -- Compare versions.
    log_info("latest release is " .. cloud_tag .. "; install type is " .. install_type .. ".")
    if did_themes_update_fail(nil, cloud_tag) then
        log_info("update again to version " .. local_tag .. " because themes directory is missing.")
    elseif not is_rhs_version_newer(nil, local_tag, cloud_tag) then
        log_info("no update available; local version " .. local_tag .. " is not older than latest release " .. cloud_tag .. ".") -- luacheck: no max line length
        return "", "already up-to-date.", true
    end

    -- Check if already downloaded.
    local local_update_file = has_update_file(install_type)
    if local_update_file and clink.lower(path.getbasename(local_update_file)) == clink.lower(cloud_tag) then
        return local_update_file
    end

    -- Download latest release update file (zip or exe).
    -- Note:  Because of github redirection, there's no way to verify whether
    -- the file existed on the server.  But if it's not a zip file then the
    -- expand will fail, and if it's not an exe file then execution will fail.
    -- That's good enough.
    if force then
        print("Downloading latest release " .. cloud_tag .. "...")
    end
    local_update_file, err = get_update_dir()
    if local_update_file then
        local_update_file = path.join(local_update_file, cloud_tag .. "." .. install_type)
    else
        return nil, err
    end
    log_info("downloading " .. latest_update_file .. " to " .. local_update_file .. ".")
    local cmd
    if clink.DEBUG and mock then
        cmd = string.format([[2>&1 copy "%s" "%s"]], latest_update_file, local_update_file)
    else
        cmd = string.format([[2>&1 ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12 ; Invoke-WebRequest '%s' -OutFile '%s'"]], latest_update_file, local_update_file) -- luacheck: no max line length
    end
    f, err = io.popen(cmd)
    if not f then
        log_info(cmd)
        os.remove(local_update_file)
        return nil, concat_error(err, log_info("failed to download " .. install_type .. " file."))
    end
    output = {}
    for line in f:lines() do
        local utf8 = unicode.fromcodepage(line)
        line = utf8 or line
        collect_output(output, line)
    end
    f:close()

    local ok = os.isfile(local_update_file)
    if ok then
        local info = os.globfiles(local_update_file, 2)
        if not info or not info[1] or not info[1].size or info[1].size == 0 then
            ok = false
        end
    end
    if not ok then
        log_output(cmd, output)
        return nil, log_info("failed to download " .. install_type .. " file.")
    end

    return local_update_file
end

local function update_lastcheck(update_dir)
    local tagfile = path.join(update_dir, tag_filename)
    local f, err = io.open(tagfile, "w")
    if f then
        f:write(tostring(os.time()) .. "\n")
        f:close()
    else
        log_info(concat_error("unable to record last update time to '" .. tagfile .. "'.", err))
    end
end

local function check_for_update(force)
    local update_dir, err = get_update_dir()
    if not update_dir then
        return nil, err
    end

    -- Protect against concurrent updates.
    local guardfile = path.join(update_dir, "updating")
    local g, err = io.sopen(guardfile, "w", "w") -- luacheck: ignore 411
    if not g then
        return nil, log_info("unable to update because another update may be in progress; " .. err), true -- luacheck: no max line length
    end

    -- Attempt to update.
    local ret, err, stop = internal_check_for_update(force) -- luacheck: ignore 411

    -- Record last update time.
    update_lastcheck(update_dir)

    -- Dispose of the concurrency protection.
    g:close()
    os.remove(guardfile)
    return ret, err, stop
end

local function is_update_ready(force)
    local exe_path, err = get_bin_dir()
    if not exe_path then
        return nil, err
    end

    local update_dir, err = get_update_dir() -- luacheck: ignore 411
    if not update_dir then
        return nil, err
    end

    local ok, err = find_prereqs() -- luacheck: ignore 411
    if not ok then
        return nil, concat_error(err, log_info("autoupdate requires PowerShell v5."))
    end

    -- Download latest update file, or use update file that's already been
    -- downloaded.
    local update_file, stop
    if force then
        local can
        can, err = can_check_for_update(force)
        if not can then
            return nil, err
        end
    end
    update_file, err, stop = check_for_update(force)
    if not update_file then
        -- If an update is already downloaded, then ignore transient errors
        -- that may have occurred while attempting to check or download a new
        -- update.
        if not stop then
            update_file = has_update_file()
        end
        if not update_file then
            -- If no update file is already downloaded, then report the error
            -- from the earlier check_for_update().
            return nil, err
        end
    elseif update_file == "" then
        return "", err
    end

    -- Verify the update file is newer.
    local local_tag = get_local_tag()
    local cloud_tag = latest_cloud_tag or path.getbasename(update_file)
    if not is_rhs_version_newer(local_tag, cloud_tag) then
        if did_themes_update_fail(exe_path, cloud_tag) then
            log_info("reapply update in " .. update_file .. " using new updater with themes directory fix.")
            return update_file
        end
        log_info("no update available; local version " .. local_tag .. " is not older than latest release " .. cloud_tag .. ".") -- luacheck: no max line length
        return "", "already up-to-date."
    end

    -- Skip the update if the user said to.
    local skip, range = clink._is_skip_update(cloud_tag)
    if skip then
        log_info("no update available; " .. cloud_tag .. " is available but user chose to skip through " .. range .. ".") -- luacheck: no max line length
        return "", "no update available."
    end

    -- Update is ready.
    log_info("update in " .. update_file .. " is ready to be applied.")
    return update_file
end

local function prepare_to_update(elevated, cloud_tag)
    local bin_dir, err = get_bin_dir()
    if not bin_dir then
        return nil, nil, err
    end

    local update_dir, err = get_update_dir() -- luacheck: ignore 411
    if not update_dir then
        return nil, nil, err
    end

    -- When elevated, assume the mutex is already acquired by the non-elevated
    -- caller.  Otherwise deadlock is possible if multiple Clink instances are
    -- waiting for the mutex.
    if not elevated and not clink._acquire_updater_mutex() then
        return
    end

    -- Must double check that cloud_tag is newer than what's running,
    -- since an update might have completed while waiting for the mutex.
    local local_tag = get_local_tag()
    if not is_rhs_version_newer(local_tag, cloud_tag) and
            not did_themes_update_fail(bin_dir, cloud_tag) then
        return nil, nil, log_info("already updated; local version " .. local_tag .. " is not older than update candidate " .. cloud_tag .. ".") -- luacheck: no max line length
    end

    return bin_dir, update_dir
end

local function apply_zip_update(elevated, zip_file)
    local cloud_tag = path.getbasename(zip_file)
    local bin_dir, update_dir, err = prepare_to_update(elevated, cloud_tag)
    if not bin_dir or not update_dir then
        return nil, err
    end

    local expand_dir = path.join(update_dir, cloud_tag)
    print("Expanding zip file...")
    log_info("expanding " .. zip_file .. " into " .. expand_dir .. ".")

    -- Prepare the temp directory; ensure it is empty (avoid copying stray
    -- pre-existing files).
    local retries_remaining = 2
::retry_delete_expand_dir::
    delete_expand_dir(expand_dir)
    if os.isdir(expand_dir) then
        if retries_remaining > 0 then
            os.sleep(1)
            retries_remaining = retries_remaining - 1
            goto retry_delete_expand_dir
        end
        return nil, log_info("temp path '" .. expand_dir .. "' already exists.")
    end
    local ok, err = os.mkdir(expand_dir) -- luacheck: no unused, ignore 411
    if not ok then
        return nil, log_info("unable to create temp path '" .. expand_dir .. "'.")
    end

    -- Expand the zip file.
    local ok, err = unzip(zip_file, expand_dir) -- luacheck: ignore 411
    if not ok then
        local nope = log_info("failed to unzip the latest release.")
        return nil, concat_error(nope, err)
    end

    -- Apply expanded files.
    local files = {}
    local dirs = { { from=expand_dir, to=bin_dir } }
    while dirs[1] do
        local d = table.remove(dirs)
        local t = os.globfiles(path.join(d.from, "*"), true)
        for _, f in ipairs(t) do
            if f.type:find("dir") then
                table.insert(dirs, { from=path.join(d.from, f.name), to=path.join(d.to, f.name) })
            else
                table.insert(files, { from=d.from, to=d.to, name=f.name })
            end
        end
    end
    for _, f in ipairs(files) do
        local code
        ok, err, code = install_file(f.from, f.to, f.name)
        if not ok then
            local ret = nil
            if code == -1 then
                ret = -1 -- Signal that a retry with elevation is needed.
            end
            local msg = string.format("failed to install file '%s' from '%s' to '%s'.", f.name, f.from, f.to)
            return ret, log_info(concat_error(msg, err))
        end
    end

    -- Update installed version.
    if this_install_type == "exe" and this_install_key then
        print("Updating registry keys...")
        clink._set_install_version(this_install_key, cloud_tag);
    end

    -- Cleanup.
    delete_expand_dir(expand_dir)
    delete_files(update_dir, "*.zip", zip_file)
    delete_files(bin_dir, "~clink.*.old")

    print("")
    return 1, log_info("updated Clink to " .. cloud_tag .. ".")
end

local function run_exe_installer(elevated, setup_exe)
    local cloud_tag = path.getbasename(setup_exe)
    local bin_dir, update_dir, err = prepare_to_update(elevated, cloud_tag)
    if not bin_dir or not update_dir then
        return nil, err
    end

    print("Launching the Clink setup program...")
    local command = setup_exe .. " /S /D=" .. bin_dir
    log_info("launching setup program '" .. command .. "'")

    -- Run the setup program.
    -- NOTE:  This can get blocked by malware protection.
    local ok, what, code = os.execute(command) -- luacheck: no unused
    if not ok or code ~= 0 then
        return nil, log_info(string.format("setup program %s.", ok and "canceled" or "failed"))
    end

    -- Cleanup.
    delete_files(update_dir, "*.exe", setup_exe)

    print("")
    return 1, log_info("updated Clink to " .. cloud_tag .. ".")
end

local function check_need_ui(update_file, mode)
    if mode == "prompt" then
        return true
    elseif mode ~= "auto" then
        return false
    elseif path.getextension(update_file):lower() ~= ".zip" then
        return true
    else
        local install_type = get_installation_type()
        return (install_type == "zip" and this_install_type == "exe")
    end
end

local function get_clink_exe()
    local exe = CLINK_EXE
    if exe then
        local n = path.getname(exe)
        if n and n:find("^[cC][lL][iI][nN][kK]_.*%.exe$") then
            return '"' .. exe .. '"'
        end
    end
end

local function prun(command)
    local f = io.popen(command)
    if f then
        for _ in f:lines() do
            -- Silently consume the output; it's a hidden async operation.
        end
        f:close()
    end
end

local function maybe_trigger_update(update_file)
    local mode = settings.get("clink.autoupdate")
    if check_need_ui(update_file, mode) then
        local exe = get_clink_exe()
        if not exe then
            log_info("unable to find Clink program.")
            return
        end

        -- Disable checking for update, to mitigate sharing violation between
        -- try_update() and the `clink update --prompt` process, due to the
        -- guard file in the temporary update directory.
        dont_check_for_update = true

        local c = coroutine.create(function()
            prun("2>nul " .. exe .. " update --prompt")
        end)
        clink.runcoroutineuntilcomplete(c)
    end
end

local function try_autoupdate(mode)
    if dont_check_for_update then
        return
    end

    local update_file = is_update_ready(false)
    if not update_file or update_file == "" then
        return
    end

    if mode ~= "auto" and mode ~= "prompt" then
        return
    end

    if check_need_ui(update_file, mode) then
        return
    end

    local exe = get_clink_exe()
    if not exe then
        log_info("unable to find Clink program.")
        return
    end

    prun("2>nul " .. exe .. " update")
end

local function print_update_message(ok, redirected, msg)
    msg = unicode.normalize(1, msg)
    for codepoint, _, _ in unicode.iter(msg) do -- luacheck: ignore 512
        msg = clink.upper(codepoint) .. msg:sub(#codepoint + 1)
        break
    end

    if redirected then
        print(msg)
    else
        local color
        if ok == true then
            color = "\x1b[0;1;32m"
        elseif ok == false then
            color = "\x1b[0;1;31m"
        else
            color = "\x1b[0;1m"
        end
        clink.print(color .. msg .. "\x1b[m")
    end
end

local function report_if_update_available(manual, redirected)
    local update_file = has_update_file()
    if update_file and can_check_for_update(true) then
        local local_tag = get_local_tag()
        local cloud_tag = path.getbasename(update_file)
        if did_themes_update_fail(nil, cloud_tag) then
            if manual and need_lf then
                print("")
            end
            print_update_message(nil, redirected, "Clink " .. cloud_tag .. " needs to be updated again to install the themes directory.") -- luacheck: no max line length
            print("- To apply the update, run 'clink update'.")
            clink.printreleasesurl("- ")
            if not manual then
                maybe_trigger_update(update_file)
            end
            return true
        end
        if is_rhs_version_newer(local_tag, cloud_tag) and
                not clink._is_skip_update(cloud_tag) and
                not clink._is_snoozed_update() then
            if manual and need_lf then
                print("")
            end
            print_update_message(nil, redirected, "Clink " .. cloud_tag .. " is available.")
            print("- To apply the update, run 'clink update'.")
            if not manual then
                print("- To stop checking for updates, run 'clink set clink.autoupdate off'.")
            end
            clink.printreleasesurl("- ")
            if not manual then
                maybe_trigger_update(update_file)
            end
            return true
        end
    end
end

--------------------------------------------------------------------------------
local function autoupdate()
    local mode = settings.get("clink.autoupdate")
    if not mode or mode == "off" then
        log_info("clink.autoupdate is off.")
        return
    end

    -- Report if an update is downloaded already.
    if report_if_update_available(false) then
        print("")
    end

    -- Possibly check for a new update.
    if can_check_for_update() or (mode == "auto" and has_update_file()) then
        local c = coroutine.create(function()
            try_autoupdate(mode)
        end)
        clink.runcoroutineuntilcomplete(c)
    end
end

clink.oninject(autoupdate)

--------------------------------------------------------------------------------
local function do_prompt(tag)
    local action = clink._show_update_prompt(tag)
    if not action then
        return -1
    elseif action == "update" then
        return 1
    else
        return 0
    end
end

--------------------------------------------------------------------------------
function clink.printreleasesurl(tag)
    tag = tag or "\n"
    print(tag .. "To view the release notes, visit the Releases page:")
    print(string.format("  https://github.com/%s/releases", github_repo))
end

--------------------------------------------------------------------------------
-- Returns:
--      ok  = Status; -1 elevate, 0 failure, 1 success.
--      msg = Message to be displayed.
-- Returning 1 by itself suppresses any further messages (e.g. "canceled").
function clink.updatenow(elevated, force_prompt, redirected)
    latest_cloud_tag = nil

    local update_file, err = is_update_ready(true)
    if not update_file then
        return nil, err
    elseif update_file == "" then -- Indicates already up-to-date.
        if err and err ~= "" then
            print_update_message(true, redirected, err)
        end
        return 1
    end

    local install_type = get_installation_type()
    local need_elevation = (not elevated and install_type == "zip" and this_install_type == "exe")
    local ext = path.getextension(update_file):lower():match("^%.(.+)$")

    if not need_elevation and ext ~= install_type then
        return nil, log_info(string.format("mismatched update type (%s) and update file (%s).", install_type, ext))
    end

    if ext ~= "zip" and ext ~= "exe" then
        return nil, log_info("unable to determine update type from '" .. update_file .. "'.")
    end

    if force_prompt then
        if not clink._acquire_updater_mutex() then
            return nil
        end

        -- Must double check that cloud_tag is newer than what's running,
        -- since an update might have completed while waiting for the mutex.
        local local_tag = get_local_tag()
        local cloud_tag = latest_cloud_tag
        if not is_rhs_version_newer(local_tag, cloud_tag) and
                not did_themes_update_fail(nil, cloud_tag) then
            log_info("already up-to-date; local version " .. local_tag .. " is not older than update candidate " .. cloud_tag .. ".") -- luacheck: no max line length
            print_update_message(true, redirected, "already up-to-date.")
            return 1
        end

        local action = do_prompt(cloud_tag)
        if action < 0 then
            return nil
        elseif action == 0 then
            return 1
        end
    end

    if need_elevation then
        log_info("elevation required to update InstallDir in registry.")
        -- IMPORTANT:  Keep the updater mutex acquired.  The elevated process
        -- assumes its caller holds the mutex.  If the elevated process had to
        -- acquire the mutex itself, then the caller would have to release the
        -- mutex, which could allow another process to sneak in and acquire
        -- the mutex before the elevated process can get it, and that leads to
        -- a hang (issue #503).
        return -1
    end

    if ext == "zip" then
        return apply_zip_update(elevated, update_file, true)
    elseif ext == "exe" then
        return run_exe_installer(elevated, update_file)
    else
        return nil, log_info("unrecognized update file type (" .. ext .. ").")
    end
end

--------------------------------------------------------------------------------
function clink.checkupdate(redirected)
    need_lf = nil
    local can, err = can_check_for_update(true)
    if can then
        clink._reset_update_keys()
        is_update_ready(true)
        if report_if_update_available(true) then
            return true
        end
        print_update_message(true, redirected, "already up-to-date.")
    elseif err then
        print_update_message(false, true, err)
    end
end
