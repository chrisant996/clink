### The Clink Lua API

#### Matches

##### clink.add_match(text)

Outputs **text** as a match for the active completion.

##### clink.compute_lcd(text, matches)

Returns the least-common-denominator of **matches**. It is assumed that **text** was the input to generate **matches**. As such it is expected that each match starts with **text**.

##### clink.get_match(index)

Returns a match by **index** from the matches output by clink.add_match().

##### clink.is_match(needle, candidate)

Given a **needle** (such as the section of the current line buffer being completed), this function returns true or false if **candidate** begins with **needle**. Readline's -/_ case-mapping is respected if it is enabled.

##### clink.is_single_match(matches)

Checks each match in the table **matches** and checks to see if they are all duplicates of each other.

##### clink.match_count()

Returns the number of matches output by calls to clink.add_match().

##### clink.match_display_filter

This variable can be set to a function so that matches can be filtered before they are displayed. See **Display Filtering** section for more info.

##### clink.matches_are_files()

Tells Readline that the matches we are passing back to it are files. This will cause Readline to append the path separator character to the line if there's only one match, and mark directories when displaying multiple matches.

##### clink.register_match_generator(generator, sort_id)

Registers a match **generator** function that is called to generate matches when the complete keys is press (TAB by default).

The generator function takes the form **generator_function(text, first, last)** where **text** is the portion of the line buffer that is to be completed, **first** and **last** are the start and end indices into the line buffer for **text**.

##### clink.set_match(index, value)

Explicitly sets match at **index** to **value**.

#### Argument Framework

##### parser:add_arguments(table1, table2, ...)

Adds more positional arguments to the parser. See **parser:set_arguments()**.

##### parser:add_flags(flag1, flag2, ...)

Adds more flags to the parser. See **parser:set_flags()**.

##### parser:be_precise()

Ordinarily Clink only loosely matches word as it traverses a parser. Calling this will make Clink only accept an exact matching word to consider moving onto the next one.

##### parser:disable_file_matching()

If this is called then Clink will not default to matching the file system if parsing comes to an end or can not be completed.

##### parser:dump()

Prints the parser to stdout.

##### parser:go(parts)

This runs the parser for the table of words **parts**. It returns a table of argument options. It is this method that Clink uses internally.

##### parser:is_flag(word)

Returns true of **word** is a valid flag.

##### parser:loop(index)

By default parsers do not loop and parsing comes to an end when there are no more arguments to traverse through. If loop() is called Clink will loop back to argument at **index** rather than terminating the parse.

##### parser:set_arguments(table1, table2, ...)

This method sets the parser's positional arguments. Each of the variable number of arguments to the method is a table of potential options for the argument at that position. Note that calling this method replaces any existing positional arguments the parser may already have. Use **parser:add_arguments()** to append more positional arguments.

##### parser:set_flags(flag1, flag2, ...)

Sets the parser's flags (which can be thought of as position independent arguments). Each argument is a string and must start with the expected flag prefix ("-" by default). Be aware that calling **set_flags()** will replace the parser's existing flags. To add more use **parser:add_flags()**.

#### Prompt Filtering

##### clink.prompt.register_filter(filter, sort_id)

Used to register a **filter** function to pre-process the prompt before use by Readline. Filters are called by **sort_id** where lower sort ids get called first. Filter functions will receive no arguments and return true if filtering is finished. Getting and setting the prompt value is done through the **clink.prompt.value** variable.

##### clink.prompt.value

User-provided prompt filter functions can get and set the prompt value using this variable.

#### Miscellaneous

##### clink.chdir(path)

Changes the current working directory to **path**. Clink caches and restores the working directory between calls to the match generation so that it does not interfere with the processes normal operation.

##### clink.find_dirs(mask, case_map)

Returns a table (array) of directories that match the supplied **mask**. If **case_map** is **true** then Clink will adjust the last part of the mask's path so that returned matches respect Readline's case-mapping feature (if it is enabled). For example; **.\foo_foo\bar_bar*** becomes **.\foo_foo\bar?bar***.

There is no support for recursively traversing the path in **mask**.

##### clink.find_files(mask, case_map)

Returns a table (array) of files that match the supplied **mask**. See **find_dirs** for details on the **case_map** argument.

There is no support for recursively traversing the path in **mask**.

##### clink.get_cwd()

Returns the current working directory.

##### clink.get_console_aliases()

Returns a table of all the registered console aliases. Windows' console alias API is exposed via **doskey** or progromatically via the AddConsoleAlias() function.

##### clink.get_env(env_var_name)

Returns the value of the environment variable **env_var_name**. This is preferable to the built-in Lua function os.getenv() as the latter uses a cached version of the current process' environment which can result in incorrect results.

##### clink.get_env_var_names()

Returns a table of the names of the current process' environment variables.

##### clink.get_host_process()

Returns the name of the host process (the rl_readline_name variable).

##### clink.get_screen_info()

Returns a table describing the current console buffer's state with the following
contents;

```
{
    -- Dimensions of the console's buffer.
    buffer_width
    buffer_height

    -- Dimensions of the visible area of the console buffer.
    window_width
    window_height
}
```

##### clink.get_setting_str(name)

Retrieves the Clink setting **name**, returning it as a string.  See **Settings** for more information on the available settings.

##### clink.get_setting_int(name)

As **clink.get_setting_str** but returning a number instead.

##### clink.is_dir(path)

Returns true if **path** resolves to a directory.

##### clink.is_rl_variable_true(readline_var_name)

Returns the boolean value of a Readline variable. These can be set with the clink_inputrc file, more details of which can be found in the [Readline manual](http://tinyurl.com/oum26rp).

##### clink.lower(text)

Same as os.lower() but respects Readline's case-mapping feature which will consider - and _ as case insensitive.

Care should be taken when using this to generate masks for file/dir find operations due to the -/_ giving different results (unless of course Readline's extended case-mapping is disabled).

##### clink.match_files(pattern, full_path, find_func)

Globs files using **pattern** and adds results as matches. If **full_path** is **true** then the path from **pattern** is prefixed to the results (otherwise only the file names are included). The last argument **find_func** is the function to use to do the globbing. If it's unspecified (or nil) Clink falls back to **clink.find_files**.

##### clink.match_words(text, words)

Calls clink.is_match() on each word in the table **words** and adds matches to Clink that match the needle **text**.

##### clink.quote_split(str, ql, qr)

This function takes the string **str** which is quoted by **ql** (the opening quote character) and **qr** (the closing character) and splits it into parts as per the quotes. A table of these parts is returned.

```
clink.quote_split("pre(middle)post", "(", ")") = {
    "pre", "middle", "post"
}
```

##### clink.slash_translation(type)

Controls how Clink will translate the path separating slashes for the current path being completed. Values for **type** are;

- -1 - no translation
- 0 - to backslashes
- 1 - to forward slashes.


##### clink.split(str, sep)

Splits the string **str** into pieces separated by **sep**, returning a table of the pieces.

##### clink.suppress_char_append()

This stops Readline from adding a trailing character when completion is finished (usually when a single match is returned). The suffixing of a character is enabled before completion functions are called so a call to this will only apply for the current completion.

By default Readline appends a space character (' ') when the is only a single match unless it is completing files where it will use the path separator instead.

##### clink.suppress_quoting()

Suppress the prefixing and suffixing of quotes even if there is a character in the current word being completed that would ordinarily need surrounding in quotes.

#### Readline Constants

Clink exposes a small amount of state from Readline in the global **rl_state** table. Readline's nomenclature is maintained (minus the *rl* prefix) so Readline's manual can also be used as reference. This table should be considered read-only - changes to the table's members are not fed back to Readline.

##### rl_state.line_buffer

This variable contains the current state of the whole line being edited.

##### rl_state.point

The current cursor position within the line buffer.

<!-- vim: wrap nolist ft=markdown
-->
