# What is Clink?

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the famous Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

<br>

# Features

- The same line editing as Bash (from GNU's Readline library).
- History persistence between sessions.
- Context sensitive completion;
  - Executables (and aliases).
  - Directory commands.
  - Environment variables
  - Thirdparty tools; Git, Mercurial, SVN, Go, and P4.
- New keyboard shortcuts;
  - Paste from clipboard (<kbd>Ctrl</kbd>-<kbd>V</kbd>).
  - Incremental history search (<kbd>Ctrl</kbd>-<kbd>R</kbd> and <kbd>Ctrl</kbd>-<kbd>S</kbd>).
  - Powerful completion (<kbd>Tab</kbd>).
  - Undo (<kbd>Ctrl</kbd>-<kbd>Z</kbd>).
  - Automatic `cd ..` (<kbd>Ctrl</kbd>-<kbd>PgUp</kbd>).
  - Environment variable expansion (<kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>E</kbd>).
  - (press <kbd>Alt</kbd>-<kbd>H</kbd> for many more...)
- Scriptable completion with Lua.
- Coloured and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds <kbd>Alt</kbd>-<kbd>H</kbd> to display the current key bindings. More features can also be found in GNU's [Readline](http://tinyurl.com/oum26rp) and [History](http://tinyurl.com/p92oq5d) libraries' manuals.

<br>

# Usage

There are three ways to use Clink the first of which is to add Clink to cmd.exe's autorun registry entry. This can be selected when installing Clink using the installer and Clink also provides the ability to manage this autorun entry from the command line. Running `clink autorun --help` has more information.

The second alternative is to manually run Clink using the command `clink inject` from within a command prompt session to run Clink in that session.

The last option is to use the Clink shortcut that the installer adds to Windows' start menu. This is in essence a shortcut to the command `cmd.exe /k clink inject`.

<br>

# How Clink Works

When running Clink via the methods above, Clink checks the parent process is supported and injects a DLL into it. The DLL then hooks the WriteConsole() and ReadConsole() Windows functions. The former is so that Clink can capture the current prompt, and the latter hook allows Clink to provide it's own Readline-powered command line editing.

<br>

# Configuring Clink

The easiest way to configure Clink is to use Clink's `set` command line option.  This can list, query, and set Clink's settings. Run `clink set --help` from a Clink-installed cmd.exe process to learn more both about how to use it and to get descriptions for Clink's various options.

The following table describes the available Clink settings;

Name                         | Description
:--:                         | -----------
`clink.paste_crlf`           | What to do with CR and LF characters on paste. Set this to `delete` to delete them, or to `space` to replace them with spaces.
`clink.path`                 | A list of paths to load Lua scripts. Multiple paths can be delimited semicolons. _**TODO:** Describe the default behavior._
`cmd.auto_answer`            | Automatically answers cmd.exe's "Terminate batch job (Y/N)?" prompts. `off` = disabled, `answer_yes` = answer Y, `answer_no` = answer N.
`cmd.ctrld_exits`            | Ctrl-D exits the process when it is pressed on an empty line.
`colour.doskey`              | Used when Clink displays doskey alias completions.
`colour.hidden`              | Used when Clink displays file completions with the "hidden" attribute.
`colour.interact`            | Used when Clink displays text or prompts such as a pager's `--More?--` prompt.
`colour.readonly`            | Used when Clink displays file completions with the "readonly" attribute.
`doskey.enhanced`            | Enhanced Doskey adds the expansion of macros that follow `|` and `&` command separators and respects quotes around words when parsing `$1`..`$9` tags. Note that these features do not apply to Doskey use in Batch files.
`exec.cwd`                   | When matching executables as the first word (`exec.enable`), include executables in the current directory. (This is implicit if the word being completed is a relative path).
`exec.dirs`                  | When matching executables as the first word (`exec.enable`), also include directories relative to the current working directory as matches.
`exec.enable`                | Only match executables when completing the first word of a line.
`exec.path`                  | When matching executables as the first word (`exec.enable`), include executables found in the directories specified in the `%PATH%` environment variable.
`exec.space_prefix`          | If the line begins with whitespace then Clink bypasses executable matching (`exec.path`) and will do normal files matching instead.
`files.hidden`               | Includes or excludes files with the "hidden" attribute set when generating file lists.
`files.system`               | Includes or excludes files with the "system" attribute set when generating file lists.
`files.unc_paths`            | UNC (network) paths can cause Clink to stutter when it tries to generate matches. Enable this if matching UNC paths is required. _**TODO:** This may become unnecessary if/when Clink can be made to only generate matches in response to completion commands._
`history.dont_add_to_history_cmds` | List of commands that aren't automatically added to the history. Commands are separated by spaces, commas, or semicolons. Default is `exit history`, to exclude both of those commands.
`history.dupe_mode`          | If a line is a duplicate of an existing history entry Clink will erase the duplicate when this is set `erase_prev`. Setting it to `ignore` will not add duplicates to the history, and setting it to `add` will always add lines.
`history.expand_mode`        | The `!` character in an entered line can be interpreted to introduce words from the history. This can be enabled and disable by setting this value to `on` or `off`. Values of `not_squoted`, `not_dquoted`, or `not_quoted` will skip any `!` character quoted in single, double, or both quotes respectively.
`history.ignore_space`       | Ignore lines that begin with whitespace when adding lines in to the history.
`history.save`               | Saves history between sessions.
`history.shared`             | When history is shared, all instances of Clink update the master history list after each command and reload the master history list on each prompt.  When history is not shared, each instance updates the master history list on exit.
`lua.debug`                  | Loads a simple embedded command line debugger when enabled. Breakpoints can be added by calling `pause()`.
`lua.path`                   | Value to append to `package.path`. Used to search for Lua scripts specified in `require()` statements.
`match.ignore_case`          | Controls case sensitivity in string comparisons. `off` = case sensitive, `on` = case insensitive, `relaxed` = case insensitive plus `-` and `_` are considered equal.
`match.sort_dirs`            | Matching directories can go before files, with files, or after files.
`terminal.emulate`           | Clink can either emulate a virtual terminal and handle ANSI escape codes itself, or let the console host natively handle ANSI escape codes. `off` = pass output directly to the console host process, `on` = clink handles ANSI escape codes itself.
`terminal.modify_other_keys` | When enabled, pressing Space or Tab with modifier keys sends extended XTerm key sequences so they can be bound separately.

<br>

> Notes:
> - The `esc_clears_line` setting has been replaced by a `clink-reset-line` command that can be bound to <kbd>Escape</kbd> (or any other key).
> - The `history_file_lines` setting doesn't exist at the moment. _**NYI:** Some mechanism for trimming history will be added eventually._
> - The `use_altgr_substitute` setting has been removed. _**NYI:** If AltGr or lack of AltGr causes a problem for you, please open an issue in the repo with details._

## File Locations

Settings and history are persisted to disk from session to session. The location of these files depends on which distribution of Clink was used. If you installed Clink using the .exe installer then Clink uses the current user's non-roaming application data directory. This user directory is usually found in one of the following locations;

- Windows XP: `c:\Documents and Settings\&lt;username&gt;\Local Settings\Application Data`
- Windows Vista onwards: `c:\Users\&lt;username&gt;\AppData\Local`

The .zip distribution of Clink creates and uses a directory called `profile` which is located in the same directory where Clink's core files are found.

All of the above locations can be overridden using the `--profile &lt;path&gt;` command line option which is specified when injecting Clink into cmd.exe using `clink inject`.

<br>

# Configuring Readline

Readline itself can also be configured to add custom keybindings and macros by creating a Readline init file. There is excellent documentation for all the options available to configure Readline in Readline's [manual](http://tinyurl.com/oum26rp).

> **TODO:** Update the description of how/where inputrc files are loaded.

Clink will search in the directory as specified by the `%HOME%` environment variable for one or all of the following files; `clink_inputrc`, `_inputrc`, and `.inputrc`. If `%HOME%` is unset then Clink will use either of the standard Windows environment variables `%HOMEDRIVE%\%HOMEPATH%` or `%USERPROFILE%`.

Other software that also uses Readline will also look for the `.inputrc` file (and possibly the `_inputrc` file too). To set macros and keybindings intended only for Clink one can use the Readline init file conditional construct like this; `$if clink [...] $endif`.

Clink also adds some new commands and configuration variables in addition to what's covered in the Readline documentation.

## New configuration variables

Name | Default | Description
:-:|:-:|---
`completion-auto-query-items`|on|Automatically prompts before displaying completions if they won't fit on one screen page.
`history-point-at-end-of-anchored-search`|off|Puts the cursor at the end of the line when using `history-search-forward` or `history-search-backward`.
`locale-sort`|on|Sorts completions with locale awareness (sort like Windows does).

<br>

> **Compatibility Note:**  The `clink_inputrc_base` file from v0.4.8 no longer exists.

## New commands

Name | Description
:-:|---
`add-history`|Adds the current line to the history without executing it, and clears the editing line.
`clink-copy-cwd`|Copy the current working directory to the clipboard.
`clink-copy-line`|Copy the current line to the clipboard.
`clink-ctrl-c`|Discards the current line and starts a new one (like Ctrl-C in CMD.EXE).
`clink-exit`|Replaces the current line with `exit` and executes it (exits the shell instance).
`clink-expand-doskey-alias`|Expand the doskey alias (if any) at the beginning of the line.
`clink-expand-env-vars`|Expand the environment variable (`%FOOBAR%`) at the cursor.
`clink-insert-dot-dot`|Inserts `..\` at the cursor.
`clink-paste`|Paste the clipboard at the cursor.
`clink-popup-complete`|Show a popup window that lists the available completions.
`clink-popup-directories`|Show a popup window of recent current working directories.  In the popup, use Enter to `cd /d` to the directory, or use Shift-Enter or Ctrl-Enter to insert directory in the editing line.
`clink-popup-history`|Show a popup window that lists the command history (if any text precedes the cursor then it uses an anchored search to filter the list).  In the popup, use Enter to execute the command, or use Shift-Enter or Ctrl-Enter to make it the current history entry.
`clink-reset-line`|Clears the current line.
`clink-scroll-bottom`|Scroll the console window to the bottom (the current input line).
`clink-scroll-line-down`|Scroll the console window down one line.
`clink-scroll-line-up`|Scroll the console window up one line.
`clink-scroll-page-down`|Scroll the console window down one page.
`clink-scroll-page-up`|Scroll the console window up one page.
`clink-scroll-top`|Scroll the console window to the top.
`clink-show-help`|Lists the currently active key bindings using friendly key names.
`clink-show-help-raw`|Lists the currently active key bindings using raw key sequences.
`clink-up-directory`|Changes to the parent directory.
`old-menu-complete-backward`|Like `old-menu-complete`, but in reverse.
`remove-history`|While searching history, removes the current line from the history.

## Popup window

The `clink-popup-complete` and `clink-popup-history` commands show a popup window that lists the available completions or the command history.  Typing does an incremental search, and <kbd>F3</kbd> or <kbd>Ctrl</kbd>+<kbd>L</kbd> go to the next match (add <kbd>Shift</kbd> to go to the previous match).  <kbd>Enter</kbd> inserts the highlighted completion or executes the highlighted history entry.  <kbd>Shift</kbd>+<kbd>Enter</kbd> jumps to the highlighted history entry without executing it.

<br>

# Extending Clink

> **WARNING:** This entire section is out of date and needs to be updated.

The Readline library allows clients to offer an alternative path for creating completion matches. Clink uses this to hook Lua into the completion process making it possible to script the generation of matches with Lua scripts. The following sections describe this in more detail and shows some examples.

## The Location of Lua Scripts

Clink looks for Lua scripts in the folders as described in the **Configuring Clink** section. By default <kbd>Ctrl</kbd>-<kbd>X</kbd>,<kbd>Ctrl</kbd>-<kbd>R</kbd> is mapped to reload all Lua scripts which can be useful when developing and iterating on your own scripts.

## Match Generators

> **TODO:** Describe the new match generator syntax.  The old syntax described below isn't compatible with v1.0.0 onward.

These are Lua functions that are registered with Clink and are called as part of Readline's completion process. Match generator functions take the following form;

```lua
function my_match_generator(text, first, last)
    -- Use text/rl_state.line_buffer to create matches,
    -- Submit matches to Clink using clink.add_match()
    -- Return true/false.
end
```

`Text` is the word that is being completed, `first` and `last` and the indices into the complete line buffer for `text` (the full line buffer can be accessed using the variable `rl_state.line_buffer`). If no further match generators need to be called then the function should return true.

Registering the match generation function is done as follows;

```lua
clink.register_match_generator(my_match_generator, sort_id)
```

The `sort_id` argument is used to sort the match generators such that generators with a lower sort ids are called first.

Here is an simple example script that checks if `text` begins with a `%` character and then uses the remainder of `text` to match the names of environment variables.

```lua
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

## Argument Completion

> **TODO:** Describe the new argument and flag syntax.  The old syntax described below isn't compatible with v1.0.0 onward.

Clink provides a framework for writing complex argument match generators in Lua.  It works by creating a parser object that describes a command's arguments and flags and then registering the parser with Clink. When Clink detects the command is being entered on the current command line being edited, it uses the parser to generate matches.

Here is an example of a simple parser for the command `foobar`;

```lua
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

As an alternative to calling `clink.arg.set_arguments()` and `clink.arg.set_flags()` you can instead provide the parser's flags and positional arguments as arguments to `clink.arg.new_parser()` as follows;

```lua
some_parser = clink.arg.new_parser(
    { "arg1-1", "arg1-2" },
    { "arg2-1", "arg2-2" },
    "-flag1", "-flag2"
)
```

### More Advanced Stuff

#### Linking Parsers

> **TODO:** Describe the new match syntax.  The old syntax described below isn't compatible with v1.0.0 onward.

There are often situations where the parsing of a command's arguments is dependent on the previous words (`git merge ...` compared to `git log ...` for example). For these scenarios Clink allows you to link parsers to arguments' words using Lua's concatenation operator. Parsers can also be concatenated with flags too.

```lua
a_parser = clink.arg.new_parser():set_arguments({"foo", "bar" })
b_parser = clink.arg.new_parser():set_arguments({ "abc", "123" })
c_parser = clink.arg.new_parser()
c_parser:set_arguments(
    { "foobar" .. b_parser },
    { c_parser }
)
```

With syntax from preceding section this converts into:

```lua
parser = clink.arg.new_parser
a_parser = parser({"foo", "bar" })
c_parser = parser(
    { "foobar" .. parser({ "abc", "123" }) },
    { c_parser }
)
```

As the example above shows, it is also possible to use a parser without concatenating it to a word. When Clink follows a link to a parser it is permanent and it will not return to the previous parser.

#### Functions As Argument Options

Argument options are not limited solely to strings. Clink also accepts functions too so more context aware argument options can be used.

```lua
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

## Filtering The Match Display

> **TODO:** Describe the new match syntax.  The old syntax described below isn't compatible with v1.0.0 onward.

In some instances it may be preferable to display potential matches in an alternative form than the generated matches passed to and used internally by Readline. This happens for example with Readline's standard file name matches, where the matches are the whole word being completed but only the last part of the path is shown (e.g. the match `foo/bar` is displayed as `bar`).

To facilitate custom match generators that may wish to do this there is the `clink.match_display_filter` variable. This can be set to a function that will then be called before matches are to be displayed.

```lua
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

The function's single argument `matches` is a table containing what Clink is going to display. The return value is a table with the input matches filtered as required by the match generator. The value of `clink.match_display_filter` is reset every time match generation is invoked.

## Customising The Prompt

> **TODO:** Describe the new prompt filter syntax.
>
> **Compatibility Note:** Both the old syntax described below **and** the new syntax are compatible with v1.1.0 onward.

Before Clink displays the prompt it filters the prompt through Lua so that the prompt can be customised. This happens each and every time that the prompt is shown which allows for context sensitive customisations (such as showing the current branch of a git repository for example).

Writing a prompt filter is straight forward and best illustrated with an example that displays the current git branch when the current directory is a git repository.

```lua
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

The filter function takes no arguments instead receiving and modifying the prompt through the `clink.prompt.value` variable. It returns true if the prompt filtering is finished, and false if it should continue on to the next registered filter.

A filter function is registered into the filter chain by passing the function to `clink.prompt.register_filter()` along with a sort id which dictates the order in which filters are called. Lower sort ids are called first.

## New Functions

> **TODO:** Document the lua functions added by Clink.

<br>

# Miscellaneous

## Binding special keys

Due to differences between Windows and Linux, escape codes for keys like PageUp/Down and the arrow keys are different in Clink. Escape codes take the format `\e[?` where `?` is one of the characters from the following table, except for a few that are listed in a separate special table.

|           |Normal     | Shift     | Ctrl      | Ctrl+Shift | Alt       | Alt+Shift | Ctrl+Alt | Ctrl+Alt+Shift |
|:-:        |:-:        |:-:        |:-:        |:-:         |:-:        |:-:        |:-:       |:-:             |
|Up         |`A`        |`1;2A`     |`1;5A`     |`1;6A`      |`1;3A`     |`1;4A`     |`1;7A`    |`1;8A`          |
|Down       |`B`        |`1;2B`     |`1;5B`     |`1;6B`      |`1;3B`     |`1;4B`     |`1;7B`    |`1;8B`          |
|Left       |`D`        |`1;2D`     |`1;5D`     |`1;6D`      |`1;3D`     |`1;4D`     |`1;7D`    |`1;8D`          |
|Right      |`C`        |`1;2C`     |`1;5C`     |`1;6C`      |`1;3C`     |`1;4C`     |`1;7C`    |`1;8C`          |
|Insert     |`2~`       |`2;2~`     |`2;5~`     |`2;6~`      |`2;3~`     |`2;4~`     |`2;7~`    |`2;8~`          |
|Delete     |`3~`       |`3;2~`     |`3;5~`     |`3;6~`      |`3;3~`     |`3;4~`     |`3;7~`    |`3;8~`          |
|Home       |`H`        |`1;2H`     |`1;5H`     |`1;6H`      |`1;3H`     |`1;4H`     |`1;7H`    |`1;8H`          |
|End        |`F`        |`1;2F`     |`1;5F`     |`1;6F`      |`1;3F`     |`1;4F`     |`1;7F`    |`1;8F`          |
|PgUp       |`5~`       |`5;2~`     |`5;5~`     |`5;6~`      |`5;3~`     |`5;4~`     |`5;7~`    |`5;8~`          |
|PgDn       |`6~`       |`6;2~`     |`6;5~`     |`6;6~`      |`6;3~`     |`6;4~`     |`6;7~`    |`6;8~`          |
|Tab        |(special)  |`Z`        |`27;5;9~`  |`27;6;9~`   |(n/a)      |(n/a)      |(n/a)     |(n/a)           |
|Space      |(special)  |(n/a)      |`27;5;32~` |`27;6;32~`  |(n/a)      |(n/a)      |`27;7;32~`|`27;8;32~`      |
|Backspace  |(special)  |(n/a)      |(special)  |(n/a)       |(special)  |(n/a)      |(n/a)     |(n/a)           |
|Escape     |(special)  |(n/a)      |(n/a)      |(n/a)       |(n/a)      |(n/a)      |(special) |(n/a)           |

<br>

These keys use other formats, so their full "special" sequences are listed in the following table.

| Key               | Special Sequence |
|:-:                |:-:        |
|Escape             |`\e\e`     |
|Tab                |`\t`       |
|Space              |(space)    |
|Backspace          |`^h`       |
|Ctrl + Backspace   |`Rubout`   |
|Alt + Backspace    |`\e^h`     |
|Ctrl + Alt + Backspace |`\eRubout` |

<br>

Here is an example line from a clink_inputrc file that binds Shift-End to the Readline `transpose-word` function;

```
"\e[1;2F": transpose-word
```

## Binding function keys

For function keys the full escape sequences are listed.  The last four columns (Alt+) are the same as the first four columns prefixed with an extra `\e`.

|           |Normal     |Shift      |Ctrl       |Ctrl+Shift  |Alt        |Alt+Shift    |Alt+Ctrl     |Alt+Ctrl+Shift  |
|:-:        |:-:        |:-:        |:-:        |:-:         |:-:        |:-:          |:-:          |:-:             |
|F1         |`\eOP`     |`\e[1;2P`  |`\e[1;5P`  |`\e[1;6P`   |`\e\eOP`   |`\e\e[1;2P`  |`\e\e[1;5P`  |`\e\e[1;6P`     |
|F2         |`\eOQ`     |`\e[1;2Q`  |`\e[1;5Q`  |`\e[1;6Q`   |`\e\eOQ`   |`\e\e[1;2Q`  |`\e\e[1;5Q`  |`\e\e[1;6Q`     |
|F3         |`\eOR`     |`\e[1;2R`  |`\e[1;5R`  |`\e[1;6R`   |`\e\eOR`   |`\e\e[1;2R`  |`\e\e[1;5R`  |`\e\e[1;6R`     |
|F4         |`\eOS`     |`\e[1;2S`  |`\e[1;5S`  |`\e[1;6S`   |`\e\eOS`   |`\e\e[1;2S`  |`\e\e[1;5S`  |`\e\e[1;6S`     |
|F5         |`\e[15~`   |`\e[15;2~` |`\e[15;5~` |`\e[15;6~`  |`\e\e[15~` |`\e\e[15;2~` |`\e\e[15;5~` |`\e\e[15;6~`    |
|F6         |`\e[17~`   |`\e[17;2~` |`\e[17;5~` |`\e[17;6~`  |`\e\e[17~` |`\e\e[17;2~` |`\e\e[17;5~` |`\e\e[17;6~`    |
|F7         |`\e[18~`   |`\e[18;2~` |`\e[18;5~` |`\e[18;6~`  |`\e\e[18~` |`\e\e[18;2~` |`\e\e[18;5~` |`\e\e[18;6~`    |
|F8         |`\e[19~`   |`\e[19;2~` |`\e[19;5~` |`\e[19;6~`  |`\e\e[19~` |`\e\e[19;2~` |`\e\e[19;5~` |`\e\e[19;6~`    |
|F9         |`\e[20~`   |`\e[20;2~` |`\e[20;5~` |`\e[20;6~`  |`\e\e[20~` |`\e\e[20;2~` |`\e\e[20;5~` |`\e\e[20;6~`    |
|F10        |`\e[21~`   |`\e[21;2~` |`\e[21;5~` |`\e[21;6~`  |`\e\e[21~` |`\e\e[21;2~` |`\e\e[21;5~` |`\e\e[21;6~`    |
|F11        |`\e[23~`   |`\e[23;2~` |`\e[23;5~` |`\e[23;6~`  |`\e\e[23~` |`\e\e[23;2~` |`\e\e[23;5~` |`\e\e[23;6~`    |
|F12        |`\e[24~`   |`\e[24;2~` |`\e[24;5~` |`\e[24;6~`  |`\e\e[24~` |`\e\e[24;2~` |`\e\e[24;5~` |`\e\e[24;6~`    |

<br>

Here is an example line from a clink_inputrc file that binds Alt-Shift-F3 to the Readline `history-substring-search-backward` function;

```
"\e\e[1;2R": history-substring-search-backward
```

## Powershell

> _**Deprecated:**  Clink v0.4.8 had some basic support for Powershell, but v1.0.0 removed Powershell support._

<!-- vim: wrap nolist ft=markdown
-->
