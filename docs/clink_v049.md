### What is Clink?

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the famous Unix shell Bash, the standard shell for many Linux distributions.

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

Settings that are loaded when Clink starts can be overridden by setting environment variables matching the setting name and prefixed with "clink.". For example, the command **set clink.prompt_colour=10** will turn the prompt green regardless of what is in the settings file. Overrides are not saved to disk.

The following table describes the available settings;

Name                         | Description
:--:                         | -----------
**ansi_code_support**        | When printing the prompt, Clink has basic built-in support for SGR ANSI escape codes to control the text colours. This is automatically disabled if a third party tool is detected that also provides this facility. It can also be disabled by setting this to 0.
**ctrld_exits**              | Ctrl-D exits the process when it is pressed on an empty line.
**esc_clears_line**          | Clink clears the current line when Esc is pressed (unless Readline's Vi mode is enabled).
**exec_match_style**         | Changes how Clink will match executables when there is no path separator on the line. 0 = PATH only, 1 = PATH and CWD, 2 = PATH, CWD, and directories. In all cases both executables and directories are matched when there is a path separator present.
**history_dupe_mode**        | If a line is a duplicate of an existing history entry Clink will erase the duplicate when this is set 2. A value of 1 will not add duplicates to the history and a value of 0 will always add lines.
**history_expand_mode**      | The '!' character in an entered line can be interpreted to introduce words from the history. This can be enabled and disable by setting this value to 1 or 0. Values or 2, 3 or 4 will skip any ! character quoted in single, double, or both quotes respectively.
**history_file_lines**       | When set to a positive integer this is the number of lines of history that will persist when Clink saves the command history to disk. Use 0 for infinite lines and &lt;0 to disable history persistence.
**history_ignore_space**     | Ignore lines that begin with whitespace when adding lines in to the history.
**history_io**               | Use this setting to control when the history is written to disk and when it is read back. A value of 1 will read the history before editing of a new line commences, 2 will write the history, and 3 will do both. The default (0) is to write the history when the process exits.",
**match_colour**             | Colour to use when displaying matches. A value less than 0 will be the opposite brightness of the default colour.
**prompt_colour**            | Surrounds the prompt in ANSI escape codes to set the prompt's colour (0..15). Disabled when the value is less than 0.
**space_prefix_match_files** | If the line begins with whitespace then Clink bypasses executable matching and will match all files and directories instead.
**terminate_autoanswer**     | Automatically answers cmd.exe's **Terminate batch job (Y/N)?** prompts. 0 = disabled, 1 = answer Y, 2 = answer N.
**use_altgr_substitute**     | Windows provides Ctrl-Alt as a substitute for AltGr, historically to support keyboards with no AltGr key. This may collide with some of Readline's bindings.

#### File Locations

Settings and history are persisted to disk from session to session. The location of these files depends on which distribution of Clink was used. If you installed Clink using the .exe installer then Clink uses the current user's non-roaming application data directory. This user directory is usually found in one of the following locations;

- c:\Documents and Settings\\&lt;username&gt;\Local Settings\Application Data *(XP)*
- c:\Users\\&lt;username&gt;\AppData\Local *(Vista onwards)*

The .zip distribution of Clink creates and uses a directory called **profile** which is located in the same directory where Clink's core files are found.

All of the above locations can be overridden using the **--profile &lt;path&gt;** command line option which is specified when injecting Clink into cmd.exe using **clink inject**.

### Configuring Readline

Readline itself can also be configured to add custom keybindings and macros by creating a Readline init file. There is excellent documentation for all the options available to configure Readline in Readline's [manual](http://tinyurl.com/oum26rp).

Clink will search in the directory as specified by the HOME environment variable for one or all of the following files; `clink_inputrc`, `_inputrc`, and `.inputrc`. If HOME is unset then Clink will use either of the standard Windows environment variables `%homedrive%\%homepath%` or `%userprofile%`.

Other software that also uses Readline will also look for the `.inputrc` file (and possibly the `_inputrc` file too). To set macros and keybindings intended only for Clink one can use the Readline init file conditional construct like this; `$if cmd.exe [...] $endif`.

Editing the `clink_inputrc_base` is discouraged as this will change from version to version and may not be present in the future.

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

Clink provides a framework for writing complex argument match generators in Lua.  It works by creating a parser object that describes a command's arguments and flags and then registering the parser with Clink. When Clink detects the command is being entered on the current command line being edited, it uses the parser to generate matches.

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

As an alternative to calling **clink.arg.set_arguments()** and **clink.arg.set_flags()** you can instead provide the parser's flags and positional arguments as arguments to **clink.arg.new_parser()** as follows;

```
some_parser = clink.arg.new_parser(
    { "arg1-1", "arg1-2" },
    { "arg2-1", "arg2-2" },
    "-flag1", "-flag2"
)
```

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

With syntax from preceding section this converts into:

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

### Miscellaneous

#### Binding special keys

Due to differences between Windows and Linux, escape codes for keys like PageUp/Down and the arrow keys are different in Clink. Escape codes take the format **\\e`?** where '?' is one of the characters from the following table;

Key      | Normal | Shift | Ctrl  | Ctrl-Shift
:-:      | :-:    | :-:   | :-:   | :-:
Home     | G      | a     | w     | !
Up       | H      | b     | T     | "
PageUp   | I      | c     | U     | #
Left     | K      | d     | s     | $
Right    | M      | e     | t     | %
End      | O      | f     | u     | &
Down     | P      | g     | V     | '
PageDown | Q      | h     | v     | (
Insert   | R      | i     | W     | )
Delete   | S      | j     | X     | \*
Tab      | (n/a)  | Z     | (n/a) | (n/a)

Here is an example line from a clink_inputrc file that binds Shift-End to the Readline function **transpose-word** function;

```
"\e`f": transpose-word
```

#### Readline's menu-complete

Clink supports Readline's menu-complete command (which is similar to vanilla cmd.exe completion that cycles through matches rather than displaying available ones). To use this menu-style completion Clink provides the alternative command **clink-menu-completion-shim**. Using this ensures that appropriate path separator translation takes place.

#### Powershell

Clink has basic support for Powershell. In order to show completion correctly Clink needs to parse Powershell's prompt to extract the current directory. If the prompt has been customized Clink is unlikely to work as expected.

<!-- vim: wrap nolist ft=markdown
-->
