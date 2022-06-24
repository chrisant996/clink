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
local reg_exe
local checked_prereqs
local prereq_error

local this_install_type
local this_install_key

local can_use_setup_exe = false

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
        reg_exe = make_file_at_path(sysroot, "System32\\reg.exe")
        checked_prereqs = true

        if not powershell_exe then
            prereq_error = log_info("unable to find PowerShell v5.")
        elseif not reg_exe then
            prereq_error = log_info("unable to find Reg.exe.")
        else
            local f = io.popen('2>&1 ' .. powershell_exe .. ' -Command "Get-Host | Select-Object Version"')
            if not f then
                powershell_exe = nil
            else
                for line in f:lines() do
                    local ver = line:match("^ *([0-9]+%.[0-9]+)")
                    if not prereq_error and ver then
                        if is_rhs_version_newer("v" .. ver, "v5.0") then
                            powershell_exe = nil
                            prereq_error = log_info("found PowerShell v" .. ver .. ".")
                        end
                    end
                end
                f:close()
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
        local ok, err = os.mkdir(target)
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

local function get_installation_type()
    if not this_install_type then
        local bin_dir, err = get_bin_dir()
        if not bin_dir then
            return nil, err
        end

        local ok, err = find_prereqs()
        if not ok then
            return nil, concat_error(err, log_info("autoupdate requires PowerShell v5."))
        end

        local done
        this_install_type = "zip"

        local r = io.popen("2>nul " .. reg_exe .. " query hklm\\software\\microsoft\\windows\\currentversion\\uninstall /reg:32")
        if r then
            for key in r:lines() do
                if key:lower():match("clink_[^/\\]+$") then
                    local i = io.popen('2>nul ' .. reg_exe .. ' query "' .. key .. '" /reg:32 /s')
                    if i then
                        for line in i:lines() do
                            local utf8 = unicode.fromcodepage(line)
                            line = utf8 or line
                            local location = line:match("^ +InstallLocation +REG_SZ +(.+)$")
                            if location and string.equalsi(location, bin_dir) then
                                this_install_key = key
                                this_install_type = "exe"
                                done = true
                                break
                            end
                        end
                        i:close()
                    end
                end
                if done then
                    break
                end
            end
            r:close()
        else
            log_info("unable to query registry for installation type.")
        end
    end

    -- This sets this_install_type to the actual installation type.  This
    -- returns "zip" if can_use_setup_exe is false, otherwise it returns the
    -- actual installation type.
    return can_use_setup_exe and this_install_type or "zip"
end

--------------------------------------------------------------------------------
local function collect_output(output, line)
    local num = #output
    if num < 10 then
        table.insert(output, line)
    elseif num == 10 then
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
    local t = os.globfiles(path.join(dir, wild))
    for _, d in ipairs(t) do
        local full = path.join(dir, d)
        if not except or not string.equalsi(full, except) then
            os.remove(full)
        end
    end
end

local function unzip(zip, out)
    if not out or out == "" or not os.isdir(out) then
        return nil, log_info("output directory '" .. tostring(out) .. "' does not exist.")
    end

    local fmt = [[2>&1 ]] .. powershell_exe .. [[ -Command $ProgressPreference='SilentlyContinue' ; Expand-Archive -Force -LiteralPath \"%s\" -DestinationPath \"%s\" ; echo $error.count]]
    local cmd = string.format(fmt, zip, out)
    local f, err = io.popen(cmd)
    if not f then
        log_info(cmd)
        return nil, err
    end

    local result
    local output = {}
    for line in f:lines() do
        local utf8 = unicode.fromcodepage(line)
        line = utf8 or line
        collect_output(output, line)
    end
    if #output == 1 and output[1] == "0" then
        result = true
    end
    f:close()

    if not result then
        log_output(cmd, output)
        return nil
    end

    return result
end

local function install_file(from_dir, to_dir, name)
    local src = path.join(from_dir, name)
    local dst = path.join(to_dir, name)

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
    ok, err2, code2 = os.move(dst, tmp)
    if not ok then
        os.remove(tmp)
        return nil, err, code
    end

    -- Retry copy.
    ok, err2, code2 = os.copy(src, dst)
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

    local latest, lmaj, lmin, lpat
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
        return false, log_info("autoupdate is disabled for local build directories.")
    end

    local ok, err = find_prereqs()
    if not ok then
        return false, concat_error(err, log_info("autoupdate requires PowerShell v5."))
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
    local local_tag = get_local_tag()
    local install_type = get_installation_type()

    -- Use github API to query latest release.
    --
    -- PowerShell needs -UseBasicParsing to prevent assuming the response is
    -- HTML and attempting to use IE to parse the DOM and execute scripts, and
    -- and needs to force TLS 1.2 because older versions of PowerShell default
    -- to using TLS 1.0.
    if force then
        print("Checking latest version...")
    end
    local cloud_tag
    local api = string.format([[2>&1 ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12 ; Invoke-WebRequest -Headers @{\"cache-control\"=\"no-cache\"} -UseBasicParsing https://api.github.com/repos/%s/releases/latest | Select-Object -ExpandProperty Content"]], github_repo)
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
    if not latest_update_file then
        log_output(api, output)
        return nil, log_info("unable to find latest release " .. install_type .. " file.")
    end

    -- Compare versions.
    log_info("latest release is " .. cloud_tag .. ".")
    if not is_rhs_version_newer(local_tag, cloud_tag) then
        return nil, log_info("no update available; local version " .. local_tag .. " is not older than latest release " .. cloud_tag .. ".")
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
        print("Downloading latest release...")
    end
    local_update_file, err = get_update_dir()
    if local_update_file then
        local_update_file = path.join(local_update_file, cloud_tag .. "." .. install_type)
    else
        return nil, err
    end
    log_info("downloading " .. latest_update_file .. " to " .. local_update_file .. ".")
    local cmd = string.format([[2>&1 ]] .. powershell_exe .. [[ -Command "$ProgressPreference='SilentlyContinue' ; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12 ; Invoke-WebRequest '%s' -OutFile '%s'"]], latest_update_file, local_update_file)
    f, err = io.popen(cmd)
    if not f then
        log_info(cmd)
        os.remove(local_update_file)
        return nil, concat_error(err, log_info("failed to download " .. install_type .. " file."))
    end
    local output = {}
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
    local g, err = io.sopen(guardfile, "w", "w")
    if not g then
        return nil, log_info("unable to update because another update may be in progress; " .. err)
    end

    -- Attempt to update.
    local ret, err = internal_check_for_update(force)

    -- Record last update time.
    update_lastcheck(update_dir)

    -- Dispose of the concurrency protection.
    g:close()
    os.remove(guardfile)
    return ret, err
end

local function is_update_ready(force)
    local exe_path, err = get_bin_dir()
    if not exe_path then
        return nil, err
    end

    local update_dir, err = get_update_dir()
    if not update_dir then
        return nil, err
    end

    local ok, err = find_prereqs()
    if not ok then
        return nil, concat_error(err, log_info("autoupdate requires PowerShell v5."))
    end

    local tagfile = path.join(update_dir, tag_filename)

    -- Download latest update file, or use update file that's already been
    -- downloaded.
    local update_file
    if force then
        local can
        can, err = can_check_for_update(force)
        if not can then
            return nil, err
        end
    end
    update_file, err = check_for_update(force)
    if not update_file then
        -- If an update is already downloaded, then ignore any error that may
        -- have occurred while attempting to check/download a new update.
        update_file = has_update_file()
        if not update_file then
            -- If no update file is already downloaded, then report the error
            -- from the earlier check_for_update().
            return nil, err
        end
    end

    -- Verify the update file is newer.
    local local_tag = get_local_tag()
    local cloud_tag = path.getbasename(update_file)
    if not is_rhs_version_newer(get_local_tag(), cloud_tag) then
        return nil, log_info("no update available; local version " .. local_tag .. " is not older than latest release " .. cloud_tag .. ".")
    end

    -- Update is ready.
    log_info("update in " .. update_file .. " is ready to be applied.")
    return update_file
end

local function apply_zip_update(zip_file, force)
    local bin_dir, err = get_bin_dir()
    if not bin_dir then
        return nil, err
    end

    local update_dir, err = get_update_dir()
    if not update_dir then
        return nil, err
    end

    local cloud_tag = path.getbasename(zip_file)
    local expand_dir = path.join(update_dir, cloud_tag)
    if force then
        print("Expanding zip file...")
    end
    log_info("expanding " .. zip_file .. " into " .. expand_dir .. ".")

    -- Prepare the temp directory; ensure it is empty (avoid copying stray
    -- pre-existing files).
    delete_files(expand_dir, "*")
    os.rmdir(expand_dir)
    if os.isdir(expand_dir) then
        return nil, log_info("temp path '" .. expand_dir .. "' already exists.")
    end
    local ok, err = os.mkdir(expand_dir)
    if not ok then
        return nil, log_info("unable to create temp path '" .. expand_dir .. "'.")
    end

    -- Expand the zip file.
    local ok, err = unzip(zip_file, expand_dir)
    if not ok then
        local nope = log_info("failed to unzip the latest release.")
        return nil, concat_error(nope, err)
    end

    -- Apply expanded files.
    local t = os.globfiles(path.join(expand_dir, "*"))
    for _, f in ipairs(t) do
        ok, err, code = install_file(expand_dir, bin_dir, f)
        if not ok then
            local ret = nil
            if code == -1 then
                ret = -1 -- Signal that a retry with elevation is needed.
            end
            return ret, log_info(concat_error("failed to install file '" .. f .. "'.", err))
        end
    end

    -- Update installed version.
    if this_install_type == "exe" and this_install_key then
        print("Updating registry keys...")
        local version = cloud_tag:gsub("^v", "")
        os.execute('>nul ' .. reg_exe .. ' add "' .. this_install_key .. '" /v DisplayName /t REG_SZ /d "Clink v' .. version .. '" /f /reg:32')
        os.execute('>nul ' .. reg_exe .. ' add "' .. this_install_key .. '" /v DisplayVersion /t REG_SZ /d "' .. version .. '" /f /reg:32')
    end

    -- Cleanup.
    delete_files(expand_dir, "*")
    os.rmdir(expand_dir)
    delete_files(update_dir, "*.zip", zip_file)
    delete_files(bin_dir, "~clink.*.old")

    return 1, log_info("updated Clink to " .. cloud_tag .. ".")
end

local function run_exe_installer(setup_exe)
    local bin_dir, err = get_bin_dir()
    if not bin_dir then
        return nil, err
    end

    local update_dir, err = get_update_dir()
    if not update_dir then
        return nil, err
    end

    local cloud_tag = path.getbasename(setup_exe)
    print("Launching the Clink setup program...")
    local command = setup_exe .. " /S /D=" .. bin_dir
    log_info("launching setup program '" .. command .. "'")
-- PROBLEM:  This gets blocked by malware protection.
    local ok, what, code = os.execute(command)
    if not ok or code ~= 0 then
        return nil, log_info(string.format("setup program %s.", ok and "canceled" or "failed"))
    end

    -- Cleanup.
    delete_files(update_dir, "*.exe", setup_exe)

    return 1, log_info("updated Clink to " .. cloud_tag .. ".")
end

local function try_autoupdate()
    is_update_ready(false)
end

--------------------------------------------------------------------------------
local function autoupdate()
    if not settings.get("clink.autoupdate") then
        log_info("clink.autoupdate is false.")
        return
    end

    -- Report if an update is downloaded already.
    local update_file = has_update_file()
    if update_file and can_check_for_update(true) then
        local tag = path.getbasename(update_file)
        if is_rhs_version_newer(get_local_tag(), tag) then
            clink.print("\x1b[1mClink " .. tag .. " is available.\x1b[m")
            print("- To apply the update, run 'clink update'.")
            print("- To stop checking for updates, run 'clink set clink.autoupdate false'.")
            print("- To view the release notes, visit the Releases page:")
            print(string.format("  https://github.com/%s/releases", github_repo))
            print("")
        end
    end

    -- Possibly check for a new update.
    if can_check_for_update() then
        local c = coroutine.create(try_autoupdate)
        clink.runcoroutineuntilcomplete(c)
    end
end

clink.oninject(autoupdate)

--------------------------------------------------------------------------------
function clink.updatenow(elevated)
    local update_file, err = is_update_ready(true)
    if not update_file then
        return nil, err
    end

    local install_type = get_installation_type()
    if not elevated and install_type == "zip" and this_install_type == "exe" then
        log_info("elevation required to update InstallDir in registry.")
        return -1
    end

    local ext = path.getextension(update_file):lower():match("^%.(.+)$")
    if ext ~= install_type then
        return nil, log_info(string.format("mismatched update type (%s) and update file (%s).", install_type, ext))
    elseif ext == "zip" then
        return apply_zip_update(update_file, true)
    elseif ext == "exe" then
        return run_exe_installer(update_file)
    else
        return nil, log_info("unable to determine update type from '" .. update_file .. "'.")
    end
end
