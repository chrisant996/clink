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
clink.test = {}
clink.test.functions = {}

local all_passed = true
local scripts_path = get_cwd()
local test_fs_path = os.getenv("tmp")
local temp_fs_list = {}
local test_sid = 0
local test_id = 0

--------------------------------------------------------------------------------
local test_fs =
{
    "file1",
    "file2",
    "case_map-1",
    "case_map_2",
    dir1 = {
        "only",
        "file1",
        "file2",
    },
    dir2 = {}
}

--------------------------------------------------------------------------------
local function colour(x)
    if no_colour ~= 0 then
        return ""
    end

    if not x then
        x = -30
    end

    return "\x1b["..tostring(30 + x).."m"
end

--------------------------------------------------------------------------------
local _print = print

local function print(x)
    _print(tostring(x)..colour())
end

--------------------------------------------------------------------------------
local function create_fs(path, fs_table)
    local cwd = get_cwd()

    mk_dir(path)
    ch_dir(path)

    if type(fs_table) ~= "table" then
        fs_table = { fs_table }
    end

    for k, v in pairs(fs_table) do
        if type(k) == "number" then
            io.open(tostring(v), "wb"):close()
        else
            create_fs(path.."/"..k, v)
        end
    end

    ch_dir(cwd)
end

--------------------------------------------------------------------------------
local function print_result(name, passed)
    result = colour(2).."pass"
    if not passed then
        result = colour(1).."fail"
    end

    local out = "  "..test_sid.."."..test_id.." "
    local function cat(x)
        out = out..x
    end

    cat(name)
    for i = 1, 48 - #out, 1 do
        cat(".")
    end
    cat(result)

    print(out)
end
    
--------------------------------------------------------------------------------
local function test_runner(name, input, expected_out, expected_matches)
    test_id = test_id + 1

    -- Skip test?
    if specific_test ~= "" then
        local should_run = false
        local tid = tostring(test_sid)
        if specific_test == tid then
            should_run = true
        end

        tid = tid.."."..tostring(test_id)
        if specific_test == tid then
            should_run = true
        end

        if not should_run then
            return
        end
    end

    local passed = true
    local output, matches = call_readline(input)

    -- Check Readline's output.
    if expected_out and expected_out ~= output then
        passed = false
    end

    -- Check Readline's generated matches.
    if expected_matches then
        table.sort(expected_matches)
        table.sort(matches)

        passed = passed and (#matches == #expected_matches)
        for _, i in ipairs(matches) do
            local found = false
            for _, j in ipairs(expected_matches) do

                if i == j then
                    found = true
                    break
                end
            end

            if found == false then
                passed = false
                break
            end
        end
    end

    print_result(name, passed)

    -- Output some context if the test failed.
    if not passed or verbose ~= 0 then
        print(colour(5).."\n    -- Results --")
        print(colour(5).."          Cwd: "..get_cwd())
        print(colour(5).."        Input: "..input:gsub("\t", "<TAB>").."_")
        print(colour(5).."       Output: "..output.."_")
        for _, i in ipairs(matches) do
            print(colour(5).."      Matches: "..i)
        end

        print(colour(5).."\n    -- Expected --")
        print(colour(5).."       Output: "..(expected_out or "<no_test>").."_")
        for _, i in ipairs(expected_matches or {}) do
            print(colour(5).."      Matches: "..i)
        end

        print("")
        error("Test failed...")
    end
end

--------------------------------------------------------------------------------
function clink.test.test_fs(fs_table, secret)
    local path = test_fs_path..os.tmpname()
    create_fs(path, fs_table)

    table.insert(temp_fs_list, path)

    ch_dir(path)
    return path
end

--------------------------------------------------------------------------------
local function pcall_test_runner(name, input, out, matches)
    local ok = pcall(test_runner, name, input, out, matches)
    if not ok then
        all_passed = false
    end
end

--------------------------------------------------------------------------------
function clink.test.test_output(name, input, expected)
    pcall_test_runner(name, input, expected, nil)
end

--------------------------------------------------------------------------------
function clink.test.test_matches(name, input, expected)
    if not expected then
        expected = test_fs
    end

    pcall_test_runner(name, input, nil, expected)
end

--------------------------------------------------------------------------------
function clink.test.run()
    -- Create FS and flatten it's source table.
    test_fs_path = clink.test.test_fs(test_fs)

    local t = {}
    for k, v in pairs(test_fs) do
        if type(k) ~= "number" then
            v = k.."\\"
        end

        table.insert(t, v)
    end
    test_fs = t

    -- Copy clink table's functions so they can be restored.
    for n, f in pairs(clink) do
        if type(f) == "function" then
            clink.test.functions[n] = f
        end
    end

    local function run_test(str)
        test_sid = test_sid + 1
        test_id = 0

        ch_dir(test_fs_path)

        for n, f in pairs(clink.test.functions) do
            clink[n] = f
        end

        print(colour(6).."Tests: "..str)

        dofile(scripts_path.."/"..str..".lua")
        print("")

        if #temp_fs_list > 1 then
            for i = 2, #temp_fs_list, 1 do
                rm_dir(temp_fs_list[i])
            end
        end
    end

    run_test("test_core")
    run_test("test_basic")
    run_test("test_quotes")
    run_test("test_dir")
    run_test("test_set")
    run_test("test_exec")
    run_test("test_env")
    run_test("test_args")

    ch_dir(scripts_path)
    rm_dir(test_fs_path)
    return all_passed
end

-- vim: expandtab
