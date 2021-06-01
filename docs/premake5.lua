-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function markdown_file(source_path, out)
    print("  << " .. source_path)

    local out_file = '.build\\docs\\'..source_path:match('([^/\\]+)$')..'.html'
    os.execute('marked -o '..out_file..' < '..source_path)

    local line_reader = io.lines(out_file)
    for line in line_reader do
        line = line:gsub("%$%(BEGINDIM%)", "<div style='opacity:0.5'>")
        line = line:gsub("%$%(ENDDIM%)", "</div>")
        out:write(line .. "\n")
    end
end

--------------------------------------------------------------------------------
local function generate_file(source_path, out)
    print("  << " .. source_path)
    local docver = _OPTIONS["docver"] or clink_git_name:upper()
    for line in io.open(source_path, "r"):lines() do
        local include = line:match("%$%(INCLUDE +([^)]+)%)")
        if include then
            generate_file(include, out)
        else
            local md = line:match("%$%(MARKDOWN +([^)]+)%)")
            if md then
                markdown_file(md, out)
            else
                line = line:gsub("%$%(CLINK_VERSION%)", docver)
                line = line:gsub("<br>", "&lt;br&gt;")
                out:write(line .. "\n")
            end
        end
    end
end

--------------------------------------------------------------------------------
local function parse_doc_tags_impl(out, file)
    print("Parse tags: "..file)

    local line_reader = io.lines(file)
    local prefix = "///"

    -- Reads a tagged line, extracting its key and value; '/// key: value'
    local function read_tagged()
        local line = line_reader()
        if not line then
            return line
        end

        local left, right = line:find("^"..prefix.."%s+")
        if not left then
            if line == prefix then
                right = #line
            else
                return nil
            end
        end

        line = line:sub(right + 1)
        local _, right, tag, value = line:find("^-([a-z]+):")
        if tag then
            _, right, value = line:sub(right + 1):find("^%s*(.+)")
            if value == nil then
                value = ""
            end
        else
            tag = "desc"
            _, _, value = line:find("^%s*(.+)")
            if not value then
                value = ""
            end
        end

        return tag, value
    end

    -- Finds '/// name: ...' tagged lines. Denotes opening of a odocument block.
    local function parse_tagged(line)
        prefix = "///"
        local left, right = line:find("^///%s+-name:%s+")
        if not left then
            prefix = "---"
            left, right = line:find("^---%s+-name:%s+")
        end

        if not left then
            return
        end

        line = line:sub(right + 1)
        local _, _, name, group = line:find("^((.+)[.:].+)$")
        if not group then
            group = "[other]"
            name = line
        end

        return group, name
    end

    for line in line_reader do
        local desc = {}

        local group, name = parse_tagged(line)
        if name then
            for tag, value in read_tagged do
                local desc_tag = desc[tag] or {}
                if value == "" and tag == "desc" then
                    desc_tag[#desc_tag] = desc_tag[#desc_tag]..'</p><p class="desc">'
                else
                    table.insert(desc_tag, value)
                end
                desc[tag] = desc_tag
            end

            desc.name = { name }

            out[group] = out[group] or {}
            table.insert(out[group], desc)
        end
    end
end

--------------------------------------------------------------------------------
local function parse_doc_tags(out, glob)
    local files = os.matchfiles(glob)
    for _, file in ipairs(files) do
        parse_doc_tags_impl(out, file)
    end
end

--------------------------------------------------------------------------------
local function bold_name(args)
    local result = {}
    if args then
        for i,v in pairs(args) do
            v = v:gsub('^([[]*)([^:]*):', '%1<span class="arg_name">%2</span>:')
            table.insert(result, v)
        end
    end
    return result
end

--------------------------------------------------------------------------------
local function do_docs()
    out_path = ".build/docs/clink.html"

    os.execute("1>nul 2>nul md .build\\docs")

    -- Collect document tags from source and output them as HTML.
    local doc_tags = {}
    parse_doc_tags(doc_tags, "clink/**.lua")
    parse_doc_tags(doc_tags, "clink/lua/**.cpp")

    local groups = {}
    for group_name, group_table in pairs(doc_tags) do
        group_table.name = group_name
        table.insert(groups, group_table)
    end

    local compare_groups = function (a, b)
        local a_other = (a.name == "[other]" and true) or false
        local b_other = (b.name == "[other]" and true) or false
        if a_other or b_other then
            if not a_other then
                return true
            else
                return false
            end
        end
        return a.name < b.name
    end

    table.sort(groups, compare_groups)

    local api_html = io.open(".build/docs/api_html", "w")
    for _, group in ipairs(groups) do
        table.sort(group, function (a, b) return a.name[1] < b.name[1] end)

        api_html:write('<div class="group">')
        api_html:write('<h5 class="group_name"><a name="'..group.name..'">'..group.name..'</a></h5>')

        for _, doc_tag in ipairs(group) do
            api_html:write('<div class="function">')

            local name = doc_tag.name[1]
            local arg = table.concat(bold_name(doc_tag.arg), ", ")
            local ret = (doc_tag.ret or { "nil" })[1]
            local var = (doc_tag.var or { nil })[1]
            local desc = table.concat(doc_tag.desc or {}, " ")
            local show = table.concat(doc_tag.show or {}, "\n")
            local deprecated = (doc_tag.deprecated or { nil })[1]
            local deprecated_class = (deprecated and " deprecated") or ""

            api_html:write('<div class="header">')
                api_html:write(' <div class="name"><a name="'..name..'">'..name..'</a></div>')
                if var then
                    api_html:write(' <div class="signature">'..var..' variable</div>')
                else
                    if #arg > 0 then
                        arg = ' '..arg..' '
                    end
                    api_html:write(' <div class="signature">('..arg..') : '..ret..'</div>')
                end
            api_html:write('</div>')

            api_html:write('<div class="body'..deprecated_class..'">')
                if deprecated then
                    api_html:write('<p class="desc"><strong>Deprecated; don\'t use this.</strong>  See <a href="#'..deprecated..'">'..deprecated..'</a> for more information.</p>')
                end
                api_html:write('<p class="desc">'..desc..'</p>')
                if #show > 0 then
                    api_html:write('<pre class="language-lua"><code>'..show..'</code></pre>')
                end
            api_html:write("</div>")

            api_html:write("</div>")
            api_html:write("<hr/>\n")
        end

        api_html:write("</div>")
    end
    api_html:close()

    -- Expand out template.
    print("")
    print(">> " .. out_path)
    generate_file("docs/clink.html", io.open(out_path, "w"))
    print("")
end



--------------------------------------------------------------------------------
newaction {
    trigger = "docs",
    description = "Generate Clink's documentation.",
    execute = do_docs,
}
