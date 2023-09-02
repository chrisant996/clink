-- Copyright (c) 2012 Martin Ridgers
-- License: http://opensource.org/licenses/MIT

--------------------------------------------------------------------------------
local function starts_with(str, start)
    return string.sub(str, 1, string.len(start)) == start
end

--------------------------------------------------------------------------------
local function make_weblink(name)
    return '<a class="wlink" href="#'..name..'"><svg width=16 height=16><use href="#wicon"/></svg><span class="wfix">.</span></a>'
end

--------------------------------------------------------------------------------
local function markdown_file(source_path, out)
    print("  << " .. source_path)

    local base_name = source_path:match('([^/\\]+)$')

    local tmp_name = '.build\\docs\\tmp.'..base_name..'.md'
    local tmp = io.open(tmp_name, "w")
    for line in io.lines(source_path) do
        local inc_file = line:match("#INCLUDE %[(.*)%]")
        if inc_file then
            for inc_line in io.lines(inc_file) do
                tmp:write(inc_line.."\n")
            end
        else
            line = line:gsub("%$%(BEGINDIM%)", "<div style='opacity:0.5'>")
            line = line:gsub("%$%(ENDDIM%)", "</div>")
            tmp:write(line .. "\n")
        end
    end
    tmp:close()

    local out_file = '.build\\docs\\'..base_name..'.html'
    os.execute('marked -o '..out_file..' < '..tmp_name)

    local line_reader = io.lines(out_file)
    for line in line_reader do
        out:write(line .. "\n")
    end
end

--------------------------------------------------------------------------------
local function generate_file(source_path, out, weblinks)
    print("  << " .. source_path)
    local docver = _OPTIONS["docver"] or clink_git_name:upper()
    local last_name
    for line in io.open(source_path, "r"):lines() do
        local include = line:match("%$%(INCLUDE +([^)]+)%)")
        if include then
            generate_file(include, out, weblinks)
        else
            local md = line:match("%$%(MARKDOWN +([^)]+)%)")
            if md then
                markdown_file(md, out)
            else
                line = line:gsub("%$%(CLINK_VERSION%)", docver)
                line = line:gsub("<br>", "&lt;br&gt;")
                line = line:gsub("<!%-%- NEXT PASS INCLUDE (.*) %-%->", "$(INCLUDE %1)")

                local n, hopen, hclose
                if weblinks then
                    n = line:match('^<p><a name="([^"]+)"')
                    hopen, hclose = line:match('^( *<h[0-9][^>]*>)(.+)$')
                    if n then
                        last_name = n
                    end
                    if hopen and not last_name then
                        last_name = hopen:match('id="([^"]+)"')
                        if not last_name then
                            error("missing name and/or id for heading:\n"..line)
                        end
                    end
                end
                if hopen then
                    out:write(hopen)
                    out:write(make_weblink(last_name))
                    out:write(hclose .. "\n")
                else
                    out:write(line .. "\n")
                end

                if hopen then
                    last_name = nil
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
local function parse_doc_tags_impl(out, file)
    print("Parse tags: "..file)

    local line_reader = io.lines(file)
    local prefix = "///"
    local desc_num = 1
    local show_num = 1
    local seen_show

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
            if tag == "show" then
                tag = tag..show_num
                seen_show = true
            end
            _, right, value = line:sub(right + 1):find("^%s*(.+)")
            if value == nil then
                value = ""
            end
        else
            if seen_show then
                desc_num = desc_num + 1
                show_num = show_num + 1
                seen_show = nil
            end
            tag = "desc"..desc_num
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

        desc_num = 1
        show_num = 1
        seen_show = nil

        return group, name
    end

    for line in line_reader do
        local desc = {}

        local group, name = parse_tagged(line)
        if name then
            for tag, value in read_tagged do
                local desc_tag = desc[tag] or {}
                if value == "" and tag:sub(1, 4) == "desc" then
                    if #desc_tag > 0 then
                        desc_tag[#desc_tag] = desc_tag[#desc_tag]..'</p><p class="desc">'
                    end
                else
                    if tag == "deprecated" then
                        group = "Deprecated"
                    end
                    table.insert(desc_tag, value)
                end
                desc[tag] = desc_tag
            end

            desc.name = { name }
            desc.desc_num = desc_num

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
    local tmp_path = ".build/docs/clink_tmp"
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
        local a_deprecated = (a.name == "Deprecated")
        local b_deprecated = (b.name == "Deprecated")
        if a_deprecated or b_deprecated then
            if not a_deprecated then
                return true
            else
                return false
            end
        end
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
    api_html:write('<h3 id="lua-api-groups">API groups</h3>')
    api_html:write('<svg style="display:none" xmlns="http://www.w3.org/2000/svg"><defs>')
    api_html:write('<symbol id="wicon" viewBox="0 0 16 16" fill="currentColor">')
    api_html:write('<path d="M4.715 6.542 3.343 7.914a3 3 0 1 0 4.243 4.243l1.828-1.829A3 3 0 0 0 8.586 5.5L8 6.086a1.002 1.002 0 0 0-.154.199 2 2 0 0 1 .861 3.337L6.88 11.45a2 2 0 1 1-2.83-2.83l.793-.792a4.018 4.018 0 0 1-.128-1.287z"/>')
    api_html:write('<path d="M6.586 4.672A3 3 0 0 0 7.414 9.5l.775-.776a2 2 0 0 1-.896-3.346L9.12 3.55a2 2 0 1 1 2.83 2.83l-.793.792c.112.42.155.855.128 1.287l1.372-1.372a3 3 0 1 0-4.243-4.243L6.586 4.672z"/>')
    api_html:write('</symbol>')
    api_html:write('</defs></svg>')
    api_html:write('<p/><div class="toc">')
    for _, group in ipairs(groups) do
        local italon = ""
        local italoff = ""
        if group.name == "Deprecated" then
            italon = "<em>"
            italoff = "</em>"
            api_html:write('<p/>')
        end
        api_html:write('<div class="H1"><a href="#'..group.name..'">')
        api_html:write(italon..group.name..italoff)
        api_html:write('</a></div>')
    end
    api_html:write('</div>')
    for _, group in ipairs(groups) do
        table.sort(group, function (a, b) return a.name[1] < b.name[1] end)

        local class group_class = "group"
        if group.name == "Deprecated" then
            group_class = group_class.." deprecated"
        end

        api_html:write('<div class="'..group_class..'">')
        api_html:write('<h5 class="group_name">'..make_weblink(group.name)..'<a name="'..group.name..'">'..group.name..'</a></h5>')

        for _, doc_tag in ipairs(group) do
            api_html:write('<div class="function">')

            local name = doc_tag.name[1]
            local arg = table.concat(bold_name(doc_tag.arg), ", ")
            local ret = (doc_tag.ret or { "nil" })[1]
            local var = (doc_tag.var or { nil })[1]
            local version = (doc_tag.ver or { nil })[1]
            local deprecated = (doc_tag.deprecated or { nil })[1]

            if not version and not deprecated then
                error('function "'..name..'" has neither -ver nor -deprecated.')
            end

            api_html:write('<div class="header">')
                if version then
                    if not version:find(' ') then
                        version = version..' and newer'
                    end
                    version = '<br/><div class="version">v'..version..'</div>'
                else
                    version = ''
                end
                api_html:write(' <div class="name">'..make_weblink(name)..'<a name="'..name..'">'..name..'</a></div>')
                if var then
                    api_html:write(' <div class="signature">'..var..' variable'..version..'</div>')
                else
                    if #arg > 0 then
                        arg = ' '..arg..' '
                    end
                    api_html:write(' <div class="signature">('..arg..') : '..ret..version..'</div>')
                end
            api_html:write('</div>') -- header

            api_html:write('<div class="body">')
                if deprecated then
                    api_html:write('<p class="desc"><strong>Deprecated; don\'t use this.</strong>')
                    if deprecated ~= "" then
                        api_html:write(' See <a href="#'..deprecated..'">'..deprecated..'</a> for more information.')
                    end
                    api_html:write('</p>')
                end
                for n = 1, doc_tag.desc_num, 1 do
                    local desc = table.concat(doc_tag["desc"..n] or {}, " ")
                    local show = table.concat(doc_tag["show"..n] or {}, "\n")
                    api_html:write('<p class="desc">'..desc..'</p>')
                    if #show > 0 then
                        api_html:write('<pre class="language-lua"><code>'..show..'</code></pre>')
                    end
                end
            api_html:write("</div>") -- body

            api_html:write("</div>") -- function
            api_html:write("<hr/>\n")
        end

        api_html:write("</div>") -- group
    end
    api_html:close()

    -- Expand out template.
    print("")
    print(">> " .. out_path)
    local tmp_file = io.open(tmp_path, "w")
    generate_file("docs/clink.html", tmp_file)
    tmp_file:close()

    -- Generate table of contents from H1 and H2 tags.
    local toc = io.open(".build/docs/toc_html", "w")
    for line in io.open(tmp_path, "r"):lines() do
        local tag, id, text = line:match('^ *<(h[12]) id="([^"]*)">(.*)</h')
        if tag then
            if text:match("<svg") then
                text = text:match("^<span.+/span>(.+)$")
            end
            toc:write('<div><a class="'..tag..'" href="#'..id..'">'..text..'</a></div>\n')
        end
    end
    toc:close()

    -- Expand out final documentation.
    local out_file = io.open(out_path, "w")
    generate_file(tmp_path, out_file, true--[[weblinks]])
    out_file:close()
    print("")
end



--------------------------------------------------------------------------------
newaction {
    trigger = "docs",
    description = "Clink: Generate Clink's documentation",
    execute = do_docs,
}
