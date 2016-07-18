-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
function clink.get_env_var_names()
    return {
        "simple",
        "case_map",
        "dash-1",
        "dash_2",
    }
end

clink.test.test_output(
    "Output basic",
    "set simp",
    "set simple"
)

clink.test.test_output(
    "Output case map",
    "set case_m",
    "set case_map"
)

clink.test.test_matches(
    "Case mapped matches",
    "set dash-",
    { "dash-1", "dash_2" }
)

clink.test.test_matches(
    "File matches after = #1",
    "set sim\t="
)

clink.test.test_matches(
    "File matches after = #2",
    "set sim\t=dir",
    { "dir1\\", "dir2\\" }
)

clink.test.test_matches(
    "File matches after = #3",
    "set sim\t=dir1\\file",
    { "file1", "file2" }
)
