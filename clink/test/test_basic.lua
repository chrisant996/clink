-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
clink.test.test_matches(
    "File system matches",
    "nullcmd "
)

clink.test.test_output(
    "Single file",
    "nullcmd file1",
    "nullcmd file1 "
)

clink.test.test_output(
    "Single dir",
    "nullcmd dir1",
    "nullcmd dir1\\"
)

clink.test.test_output(
    "Dir slash flip",
    "nullcmd dir1/",
    "nullcmd dir1\\"
)

clink.test.test_output(
    "Path slash flip",
    "nullcmd dir1/on",
    "nullcmd dir1\\only "
)

clink.test.test_matches(
    "Case mapping matches",
    "nullcmd case-m\t",
    { "case_map-1", "case_map_2" }
)

clink.test.test_output(
    "Case mapping output",
    "nullcmd case-m",
    "nullcmd case_map"
)

clink.test.test_output(
    "Case mapping complex",
    "nullcmd case_map-",
    "nullcmd case_map-"
)
