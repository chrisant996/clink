### What is Clink?

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the famous Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

### Features

- The same line editing as Bash (from GNU's Readline library).
- History persistence between sessions.
- Context sensitive completion;
 - Executables (and aliases).
 - Directory commands.
 - Environment variables
 - Thirdparty tools; Git, Mercurial, SVN, Go, and P4.
- New keyboard shortcuts;
 - Paste from clipboard (**Ctrl-V**).
 - Incremental history search (**Ctrl-R/Ctrl-S**).
 - Powerful completion (**TAB**).
 - Undo (**Ctrl-Z**).
 - Automatic "cd .." (**Ctrl-Alt-U**).
 - Environment variable expansion (**Ctrl-Alt-E**).
 - (press **Alt-H** for many more...)
- Scriptable completion with Lua.
- Coloured and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds **Alt-H** to display the current key bindings. More features can also be found in GNU's [Readline](http://tinyurl.com/oum26rp) and [History](http://tinyurl.com/p92oq5d) libraries' manuals.

### Usage

There are three ways to use Clink the first of which is to add Clink to cmd.exe's autorun registry entry. This can be selected when installing Clink using the installer and Clink also provides the ability to manage this autorun entry from the command line. Running **clink autorun --help** has more information.

The second alternative is to manually run Clink using the command **clink inject** from within a command prompt session to run Clink in that session.

The last option is to use the Clink shortcut that the installer adds to Windows' start menu. This is in essence a shortcut to the command **cmd.exe /k clink inject**.

### How Clink Works

When running Clink via the methods above, Clink checks the parent process is supported and injects a DLL into it. The DLL then hooks the WriteConsole() and ReadConsole() Windows functions. The former is so that Clink can capture the current prompt, and the latter hook allows Clink to provide it's own Readline-powered command line editing.

### Configuring Clink

The easiest way to configure Clink is to use Clink's **set** command line option.  This can list, query, and set Clink's settings. Run **clink set --help** from a Clink-installed cmd.exe process to learn more both about how to use it and to get descriptions for Clink's various options.

Configuring Clink by and large involves configuring Readline by creating a **clink_inputrc** file. There is excellent documentation for all the options available to configure Readline in Readline's [manual](http://tinyurl.com/oum26rp).

Where Clink looks for **clink_inputrc** (as well as .lua scripts and the settings file) depends on which distribution of Clink was used. If you installed Clink using the .exe installer then Clink uses the current user's non-roaming application data folder. This user directory is usually found in one of the following locations;

- c:\Documents and Settings\\<username\>\Local Settings\Application Data (XP)
- c:\Users\\<username\>\AppData\Local (Vista onwards)

The .zip distribution of Clink uses a **profile** folder in the same folder where Clink's core files are found.

All of the above locations can be overriden using the **--profile <path>** command line option which is specified when injecting Clink into cmd.exe using **clink inject**.

#### Settings

It is also possible to configure settings specific to Clink. These are stored in a file called **settings** which is found in one of the locations mentioned in the previous section. The settings file gets created the first time Clink is run.

The following table describes the available settings;

Name                     | Description
----                     | -----------
**ctrld_exits**          | Ctrl-D exits the process when it is pressed on an empty line.
**esc_clears_line**      | Clink clears the current line when Esc is pressed (unless Readline's Vi mode is enabled).
**exec_match_style**     | Changes how Clink will match executables when there is no path separator on the line. 0 = PATH only, 1 = PATH and CWD, 2 = PATH, CWD, and directories. In all cases both executables and directories are matched when there is a path separator present.
**match_colour**         | Colour to use when displaying matches. A value less than 0 will be the opposite brightness of the default colour.
**prompt_colour**        | Surrounds the prompt in ANSI escape codes to set the prompt's colour. Disabled when the value is less than 0.
**terminate_autoanswer** | Automatically answers cmd.exe's **Terminate batch job (Y/N)?** prompts. 0 = disabled, 1 = answer Y, 2 = answer N.
**persist_history**      | Enable or disable the saving and loading of command history between sessions.

### Extending Clink

The Readline library allows clients to offer an alternative path for creating completion matches. Clink uses this to hook Lua into the completion process making it possible to script the generation of matches with Lua scripts. The following sections describe this in more detail and shows some examples.

#### The Location of Lua Scripts

Clink looks for Lua scripts in the folders as described in the **Configuring Clink** section. By default **Ctrl-Q** is mapped to reload all Lua scripts which can be useful when developing and iterating on your own scripts.

#### Match Generators

These are Lua functions that are registered with Clink and are called as part of Readline's completion process. Match generator functions take the following form;

```
function my_match_generator(text, first, last)
    -- Use text/rl_state.line_buffer to create matches,
    -- Submit matches to Clink using clink.add_match()
    -- Return true/false.
end
```

**Text** is the word that is being completed, **first** and **last** and the indices into the complete line buffer for **text** (the full line buffer can be accessed using the variable **rl_state.line_buffer**). If no further match generators need to be called then the function should return true.

Registering the match generation function is done as follows;

```
clink.register_match_generator(my_match_generator, sort_id)
```

The **sort_id** argument is used to sort the match generators such that generators with a lower sort ids are called first.

Here is an simple example script that checks if **text** begins with a **%** character and then uses the remained of **text** to match the names of environment variables.

```
function env_vars_match_generator(text, first, last)
    if not text:find("^%%") then
        return false
    end

    text = clink.lower(text:sub(2))
    local text_len = #text
    for _, name in ipairs(clink.get_env_var_names()) do
        if clink.lower(name:sub(1, text_len)) == text then
            clink.add_match('%'..name..'%')
        end
    end

    return true
end

clink.register_match_generator(env_vars_match_generator, 10)
```

#### Argument Completion

Clink provides a framework for writing complex argument match generators in Lua.  It works by creating a parser object that describes a command's arguments and flags and then registering the parser with Clink. When Clink detects the command is being entered on the current command line being editied, it uses the parser to generate matches.

Here is an example of a simple parser for the command **foobar**;

```
my_parser = clink.arg.new_parser()
my_parser:set_flags("-foo", "-bar")
my_parser:set_arguments(
    { "hello", "hi" },
    { "world", "wombles" }
)

clink.arg.register_parser("foobar", my_parser)
```

This parser describes a command that has two positional arguments each with two potential options. It also has two flags which the parser considers to be position independent meaning that provided the word being completed starts with a certain prefix the parser with attempt to match the from the set of flags.

On the command line completion would look something like this;

```
C:\>foobar hello -foo wo
world   wombles
C:\>foobar hello -foo wo_
```

Also you can provide parser flags and arguments directly into `clink.arg.new_parser()` like following:

```
some_parser = clink.arg.new_parser(
    {arg1-1, arg1-2, ...},
    {arg2-1, arg2-2, ...},
    flag1, flag2...)
```

You may replace `clink.arg.new_parser` with short name like

```
parser = clink.arg.new_parser
```

and use in your definition.

##### More Advanced Stuff

###### Linking Parsers

There are often situations where the parsing of a command's arguments is dependent on the previous words (**git merge ...** compared to **git log ...** for example). For these scenarios Clink allows you to link parsers to arguments' words using Lua's concatenation operator. Parsers can also be concatenated with flags too.

```
a_parser = clink.arg.new_parser():set_arguments({"foo", "bar" })
b_parser = clink.arg.new_parser():set_arguments({ "abc", "123" })
c_parser = clink.arg.new_parser()
c_parser:set_arguments(
    { "foobar" .. b_parser },
    { c_parser }
)
```

With syntax from preceeded section this converts into:

```
parser = clink.arg.new_parser
a_parser = parser({"foo", "bar" })
c_parser = parser(
    { "foobar" .. parser({ "abc", "123" }) },
    { c_parser }
)
```

As the example above shows, it is also possible to use a parser without concatenating it to a word. When Clink follows a link to a parser it is permanent and it will not return to the previous parser.

###### Functions As Argument Options

Argument options are not limited solely to strings. Clink also accepts functions too so more context aware argument options can be used.

```
function rainbow_function(word)
    return { "red", "white", "blue" }
end

the_parser = clink.arg.new_parser()
the_parser:set_arguments(
    { "zippy", "bungle", "george" },
    { rainbow_function, "yellow", "green" }
)
```

The functions take a single argument which is a word from the command line being edited (or partial word if it is the one under the cursor). Functions should return a table of potential matches (or an empty table if it calls clink.add_match() directly itself).

#### Filtering The Match Display

In some instances it may be preferable to display potential matches in an alternative form than the generated matches passed to and used internally by Readline. This happens for example with Readline's standard file name matches, where the matches are the whole word being completed but only the last part of the path is shown (e.g. the match **foo/bar** is displayed as **bar**).

To facilitate custom match generators that may wish to do this there is the **clink.match_display_filter** variable. This can be set to a function that will then be called before matches are to be displayed.

```
function my_display_filter(matches)
    new_matches = {}

    for _, m in ipairs(matches) do
        local _, _, n = m:find("\\([^\\]+)$")
        table.insert(new_matches, n)
    end

    return new_matches
end

function my_match_generator(text, first, last)
    ...

    clink.match_display_filter = my_display_filter
    return true
end
```

The function's single argument **matches** is a table containing what Clink is going to display. The return value is a table with the input matches filtered as required by the match generator. The value of **clink.match_display_filter** is reset every time match generation is invoked.

#### Customising The Prompt

Before Clink displays the prompt it filters the prompt through Lua so that the prompt can be customised. This happens each and every time that the prompt is shown which allows for context sensitive customisations (such as showing the current branch of a git repository for example).

Writing a prompt filter is straight forward and best illustrated with an example that displays the current git branch when the current directory is a git repository.

```
function git_prompt_filter()
    for line in io.popen("git branch 2>nul"):lines() do
        local m = line:match("%* (.+)$")
        if m then
            clink.prompt.value = "["..m.."] "..clink.prompt.value
            break
        end
    end

    return false
end

clink.prompt.register_filter(git_prompt_filter, 50)
```

The filter function takes no arguments instead receiving and modifying the prompt through the **clink.prompt.value** variable. It returns true if the prompt filtering is finished, and false if it should continue on to the next registered filter.

A filter function is registered into the filter chain by passing the function to **clink.prompt.register_filter()** along with a sort id which dictates the order in which filters are called. Lower sort ids are called first.

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

This method sets the parsers positional arguments. Each of the variable number of arguments to the method is a table of potential options for the argument at that position.

##### parser:set_flags(flag1, flag2, ...)

Sets the parser's flags (which can be thought of as position independent arguments). Each argument is a string and must start with the expected flag prefix ("-" by default).

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

### Usage

#### Binding special keys

Due to differences between Windows and Linux, escape codes for keys like PageUp/Down and the arrow keys are different in Clink. Escape codes take the format **\e`?** where '?' is one of the characters from the following table;

Key      | Normal | Shift | Ctrl | Ctrl-Shift
:-:      | :-:    | :-:   | :-:  | :-:
Home     | G      | a     | w    | !
Up       | H      | b     | T    | "
PageUp   | I      | c     | U    | #
Left     | K      | d     | s    | $
Right    | M      | e     | t    | %
End      | O      | f     | u    | &
Down     | P      | g     | V    | '
PageDown | Q      | h     | v    | (
Insert   | R      | i     | W    | )
Delete   | S      | j     | X    | *

Here is an example line from a clink_inputrc file that binds Shift-End to the Readline function **transpose-word** function;

```
"\e`f": transpose-word
```

#### Powershell

Clink has basic support for Powershell. In order to show completion correctly Clink needs to parse Powershell's prompt to extract the current directory. If the prompt has been customized Clink is unlikely to work as expected.

<!-- vim: wrap nolist ft=markdown
-->
