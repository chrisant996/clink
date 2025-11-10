-- Copyright (c) 2025 Christopher Antos
-- License: http://opensource.org/licenses/MIT

-- luacheck: globals http
http = http or {}

--------------------------------------------------------------------------------
--- -name:  http.request
--- -ver:   1.9.0
--- -arg:   method:string
--- -arg:   url:string
--- -arg:   [options:table]
--- -ret:   string, table
--- Issues a web request and returns the response body and a table with
--- information about the response (including the status code, which indicates
--- success or failure).
---
--- When called from a coroutine this yields until the request is complete,
--- otherwise it is a blocking call.
---
--- The <span class="arg">method</span> argument is the request type
--- (<code>"GET"</code>, <code>"POST"</code>, etc).
---
--- The <span class="arg">url</span> argument is the URL to call.
---
--- The <span class="arg">options</span> argument is optional.  It may be a
--- table containing any of the following fields:
--- <ul>
--- <li><code>user_agent</code> = The user agent string.
--- <li><code>no_cache</code> = A boolean value indicating whether to bypass
--- caching.
--- <li><code>headers</code> = A table of key=value pairs describing
--- additional request headers.
--- <li><code>body</code> = Optional body content for the request.
--- </ul>
---
--- The returned response info table may include any of the following fields:
--- <ul>
--- <li><code>win32_error</code> = A WIN32 error code, if any.
--- <li><code>win32_error_text</code> = A WIN32 error message string, if any.
--- <li><code>status_code</code> = The HTTP status code (e.g. 200), if any.
--- <li><code>status_text</code> = The HTTP status text (e.g. "OK"), if any.
--- <li><code>raw_headers</code> = Raw headers from the response, if any.
--- <li><code>content_type</code> = Content type in the response, if any.
--- <li><code>content_length</code> = Content length, in bytes, if any.
--- <li><code>completed_read</code> = True indicates that the response content
--- was fully read.
--- </ul>
http.request = function (method, url, options)
    local ay, response = http._request_internal(method, url, options)
    if response then
        if ay then
            while not ay:ready() do
                coroutine.yield()
            end
        end
        return response:result()
    end
end
