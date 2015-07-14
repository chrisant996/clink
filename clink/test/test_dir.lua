-- Copyright (c) 2013 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink.test.test_fs({
    one_dir = { "leaf" },
    two_dir = { "leaf" },
    three_dir = { "leaf" },
    nest_1 = { nest_2 = { "leaf" } },

    "one_file",
    "two_file",
    "three_file",
    "four_file",
})

--------------------------------------------------------------------------------
for _, i in ipairs({"cd", "rd", "rmdir", "md", "mkdir", "pushd"}) do
    clink.test.test_matches(
        "Matches: "..i,
        i.." t",
        { "two_dir\\", "three_dir\\" }
    )

    clink.test.test_output(
        "Single (with -/_): "..i,
        i.." one-",
        i.." one_dir\\ "
    )

    clink.test.test_output(
        "Relative: "..i,
        i.." o\\..\\o",
        i.." o\\..\\one_dir\\ "
    )

    clink.test.test_matches(
        "No matches: "..i,
        i.." f",
        {}
    )

    clink.test.test_output(
        "Nested (forward slash): "..i,
        i.." nest_1/ne",
        i.." nest_1\\nest_2\\ " 
    )
end
