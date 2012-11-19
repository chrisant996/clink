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

-- vim: expandtab
