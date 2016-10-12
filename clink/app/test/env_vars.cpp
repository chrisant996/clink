// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#if TODO

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
    "nullcmd %simp",
    "nullcmd %simple%"
)

clink.test.test_output(
    "Second %var%",
    "nullcmd %simple% %sim",
    "nullcmd %simple% %simple%"
)

clink.test.test_output(
    "Output case map",
    "nullcmd %case_m",
    "nullcmd %case_map%"
)

clink.test.test_matches(
    "Case mapped matches",
    "nullcmd %dash-",
    { "%dash-1%", "%dash_2%" }
)

clink.test.test_matches(
    "Mid-word",
    "nullcmd One%Two%Three%dash",
    { "%dash-1%", "%dash_2%" }
)

clink.test.test_output(
    "Not in quotes",
    "nullcmd \"arg\" %simp",
    "nullcmd \"arg\" %simple%"
)

clink.test.test_matches(
    "In quotes",
    "nullcmd \"arg %dash",
    { "%dash-1%", "%dash_2%" }
)

clink.test.test_matches(
    "File matches follow %var%<TAB>",
    "nullcmd %null_env_var%"
)

#endif // TODO
