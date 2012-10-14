--
-- Copyright (c) 2012 Martin Ridgers
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_release",
    description = "Prepares a release of clink.",
    execute = function ()
        -- Helper funciton to show executed commands to TTY
        local exec = function(cmd)
            print("----------------------------------------------")
            print("-- EXEC: "..cmd)
            print("--\n")
            return os.execute(cmd)
        end

        -- Crude repurpose of this function so it can be used for nightlies
        local nightly = nil
        if clink_ver:lower() == "nightly" then
            clink_ver = "dev"
            nightly = os.getenv("CLI_ENV")..".\\..\\clink\\builds\\"

            if not os.isdir(nightly..".") then
                print("Invalid nightly directory '"..nightly.."'")
                return
            end

            local mask = path.translate(nightly, "/").."*"
            local hash = get_last_git_commit()
            local chop = 0 - hash:len()
            for _, i in ipairs(os.matchdirs(mask)) do
                if i:sub(chop) == hash then
                    print(hash.." already built")
                    return
                end
            end
        end

        local git_checkout = clink_ver
        clink_ver = clink_ver:upper()

        -- Build the output directory name
        local target_dir
        if nightly then
            target_dir = nightly
            target_dir = target_dir..os.date("%Y%m%d_")
            target_dir = target_dir..get_last_git_commit()
        else
            target_dir = ".build\\release\\"
            if clink_ver ~= "DEV" then
                target_dir = target_dir..os.date("%Y%m%d_%H%M%S_")
            end
            target_dir = target_dir..clink_ver
        end

        target_dir = path.translate(path.getabsolute(target_dir)).."\\"

        if not os.isdir(target_dir..".") then
            exec("rd /q /s "..target_dir)
            exec("md "..target_dir)
        end

        -- If we're not building DEV, create a clone and checkout correct version
        -- and build it.
        if clink_ver ~= "DEV" then
            repo_path = "clink_"..clink_ver.."_src"
            local code_dir = target_dir..repo_path
            if not os.isdir(code_dir..".") then
                exec("md "..code_dir)
            end

            -- clone repro in release folder and checkout the specified version
            exec("git clone . "..code_dir)
            if not os.chdir(code_dir) then
                print("Failed to chdir to '"..code_dir.."'")
                return
            end
            exec("git checkout "..git_checkout)
        end
        local src_dir_name = path.translate(path.getabsolute("."), "\\")

        -- Build the code.
        exec("premake4 --clink_ver="..clink_ver.." vs2010")
        exec("msbuild /m /v:q /p:configuration=release /p:platform=win32 .build/vs2010/clink.sln")
        exec("msbuild /m /v:q /p:configuration=release /p:platform=x64 .build/vs2010/clink.sln")

        local src = ".build\\vs2010\\bin\\release\\"
        local dest = target_dir.."clink_"..clink_ver

        -- Do a coarse check to make sure there's a build available.
        if not os.isdir(src..".") then
            print("There's no build available in '"..src.."'")
            return
        end

        -- Copy release files to a directory.
        local manifest = {
            "clink.bat",
            "clink*.exe",
            "clink*.dll",
            "clink*.lua",
            "clink_inputrc",
            "CHANGES",
            "LICENSE",

            "clink_dll_x*.pdb",
        }

        exec("md "..dest)
        for _, mask in ipairs(manifest) do
            exec("copy "..src..mask.." "..dest)
        end

        -- Lump lua files together.
        exec("move "..dest.."\\clink.lua "..dest.."\\clink._lua")

        local lua_lump = io.open(dest.."\\clink._lua", "a")
        for _, i in ipairs(os.matchfiles(dest.."/*.lua")) do
            i = path.translate(i)
            print("lumping "..i)

            lua_lump:write("\n--------------------------------------------------------------------------------\n")
            lua_lump:write("-- ", path.getname(i), "\n")
            lua_lump:write("--\n\n")
            for l in io.lines(i) do lua_lump:write(l, "\n") end
        end
        lua_lump:close()

        exec("del /q "..dest.."\\*.lua")
        exec("move "..dest.."\\clink._lua "..dest.."\\clink.lua")

        -- Generate documentation.
        exec("premake4 --clink_ver="..clink_ver.." clink_docs")
        exec("copy .build\\docs\\clink.html "..dest)

        -- Build the installer.
        local nsis_cmd = "makensis"
        nsis_cmd = nsis_cmd.." /DCLINK_BUILD="..dest
        nsis_cmd = nsis_cmd.." /DCLINK_VERSION="..clink_ver
        nsis_cmd = nsis_cmd.." /DCLINK_SOURCE="..src_dir_name
        nsis_cmd = nsis_cmd.." "..src_dir_name.."/installer/clink.nsi"
        exec(nsis_cmd)

        -- Tidy up code directory.
        if clink_ver ~= "DEV" and not nightly then
            exec("rd /q /s .build")
            exec("rd /q /s .git")
            exec("del /q .gitignore")

            os.chdir(target_dir)
            exec("7z a -r "..target_dir.."clink_"..clink_ver.."_src.zip "..src_dir_name)
            exec("rd /q /s "..src_dir_name)
        end

        -- Move PDBs out of the way, package release up as zips
        os.chdir(dest)
        exec("move *.pdb ..")
        exec("7z a -r ../clink_"..clink_ver..".zip ../clink_"..clink_ver)
        exec("7z a -r ../clink_"..clink_ver.."_pdb.zip ../*.pdb")
        exec("del /q ..\\*.pdb")

        if nightly then
            os.chdir(dest..".\\..")
            exec("rd /q /s "..dest)
        end
    end
}

--------------------------------------------------------------------------------
newaction {
    trigger = "clink_install_state",
    description = "Displays install state of clink.",
    execute = function ()
        function exec(cmd)
            print("\n## "..cmd.."\n##")
            os.execute(cmd.."2>nul")
        end

        exec('dir /s /b "%programfiles(x86)%\\clink"')
        exec('dir /s /b "%localappdata%\\clink"')
        exec('dir /s /b "%allusersprofile%\\clink"')
        exec('dir /s /b "%allusersprofile%\\Microsoft\\Windows\\Start Menu\\Programs\\clink"')
        exec('c:\\windows\\sysnative\\cmd.exe /c reg query "hklm\\software\\wow6432node\\microsoft\\windows\\currentversion\\uninstall" /s | findstr clink')
        exec('c:\\windows\\sysnative\\cmd.exe /c reg query "hklm\\software\\wow6432node\\microsoft\\command processor" /v autorun')
        exec('c:\\windows\\sysnative\\cmd.exe /c reg query "hklm\\software\\microsoft\\command processor" /v autorun')
    end
}
