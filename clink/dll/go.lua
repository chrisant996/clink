--
-- Copyright (c) 2013 Dobroslaw Zybort
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
local function flags(...)
    local p = clink.arg.new_parser()
    p:set_flags(...)
    return p
end

--------------------------------------------------------------------------------
local go_tool_parser = clink.arg.new_parser()
go_tool_parser:set_flags("-n")
go_tool_parser:set_arguments({
    "8a", "8c", "8g", "8l", "addr2line", "cgo", "dist", "nm", "objdump",
    "pack",
    "cover" .. flags("-func", "-html", "-mode", "-o", "-var"),
    "fix"   .. flags("-diff", "-force", "-r"),
    "prof"  .. flags("-p", "-t", "-d", "-P", "-h", "-f", "-l", "-r", "-s",
                     "-hs"),
    "pprof" .. flags(-- Options:
                     "--cum", "--base", "--interactive", "--seconds",
                     "--add_lib", "--lib_prefix",
                     -- Reporting Granularity:
                     "--addresses", "--lines", "--functions", "--files",
                     -- Output type:
                     "--text", "--callgrind", "--gv", "--web", "--list",
                     "--disasm", "--symbols", "--dot", "--ps", "--pdf",
                     "--svg", "--gif", "--raw",
                     -- Heap-Profile Options:
                     "--inuse_space", "--inuse_objects", "--alloc_space",
                     "--alloc_objects", "--show_bytes", "--drop_negative",
                     -- Contention-profile options:
                     "--total_delay", "--contentions", "--mean_delay",
                     -- Call-graph Options:
                     "--nodecount", "--nodefraction", "--edgefraction",
                     "--focus", "--ignore", "--scale", "--heapcheck",
                     -- Miscellaneous:
                     "--tools", "--test", "--help", "--version"),
    "vet"   .. flags("-all", "-asmdecl", "-assign", "-atomic", "-buildtags",
                     "-composites", "-compositewhitelist", "-copylocks",
                     "-methods", "-nilfunc", "-printf", "-printfuncs",
                     "-rangeloops", "-shadow", "-shadowstrict", "-structtags",
                     "-test", "-unreachable", "-v"),
    "yacc"  .. flags("-l", "-o", "-p", "-v"),
})

--------------------------------------------------------------------------------
local go_parser = clink.arg.new_parser()
go_parser:set_arguments({
    "env",
    "fix",
    "version",
    "build"    .. flags("-o", "-a", "-n", "-p", "-installsuffix", "-v", "-x",
                        "-work", "-gcflags", "-ccflags", "-ldflags",
                        "-gccgoflags", "-tags", "-compiler", "-race"),
    "clean"    .. flags("-i", "-n", "-r", "-x"),
    "fmt"      .. flags("-n", "-x"),
    "get"      .. flags("-d", "-fix", "-t", "-u",
                        -- Build flags
                        "-a", "-n", "-p", "-installsuffix", "-v", "-x",
                        "-work", "-gcflags", "-ccflags", "-ldflags",
                        "-gccgoflags", "-tags", "-compiler", "-race"),
    "install"  .. flags(-- All `go build` flags
                        "-o", "-a", "-n", "-p", "-installsuffix", "-v", "-x",
                        "-work", "-gcflags", "-ccflags", "-ldflags",
                        "-gccgoflags", "-tags", "-compiler", "-race"),
    "list"     .. flags("-e", "-race", "-f", "-json", "-tags"),
    "run"      .. flags("-exec",
                        -- Build flags
                        "-a", "-n", "-p", "-installsuffix", "-v", "-x",
                        "-work", "-gcflags", "-ccflags", "-ldflags",
                        "-gccgoflags", "-tags", "-compiler", "-race"),
    "test"     .. flags(-- Local.
                        "-c", "-file", "-i", "-cover", "-coverpkg",
                        -- Build flags
                        "-a", "-n", "-p", "-x", "-work", "-ccflags",
                        "-gcflags", "-exec", "-ldflags", "-gccgoflags",
                        "-tags", "-compiler", "-race", "-installsuffix", 
                        -- Passed to 6.out
                        "-bench", "-benchmem", "-benchtime", "-covermode",
                        "-coverprofile", "-cpu", "-cpuprofile", "-memprofile",
                        "-memprofilerate", "-blockprofile",
                        "-blockprofilerate", "-outputdir", "-parallel", "-run",
                        "-short", "-timeout", "-v"),
    "tool"     .. go_tool_parser,
    "vet"      .. flags("-n", "-x"),
})

--------------------------------------------------------------------------------
local go_help_parser = clink.arg.new_parser()
go_help_parser:set_arguments({
    "help" .. clink.arg.new_parser():set_arguments({
        go_parser:flatten_argument(1)
    })
})

--------------------------------------------------------------------------------
local godoc_parser = clink.arg.new_parser()
godoc_parser:set_flags(
    "-zip", "-write_index", "-analysis", "-http", "-server", "-html","-src",
    "-url", "-q", "-v", "-goroot", "-tabwidth", "-timestamps", "-templates",
    "-play", "-ex", "-links", "-index", "-index_files", "-maxresults",
    "-index_throttle", "-notes", "-httptest.serve"
)

--------------------------------------------------------------------------------
local gofmt_parser = clink.arg.new_parser()
gofmt_parser:set_flags(
    "-cpuprofile", "-d", "-e", "-l", "-r", "-s", "-w"
)

--------------------------------------------------------------------------------
clink.arg.register_parser("go", go_parser)
clink.arg.register_parser("go", go_help_parser)
clink.arg.register_parser("godoc", godoc_parser)
clink.arg.register_parser("gofmt", gofmt_parser)
