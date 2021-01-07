# What is Clink?

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the well-known Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

# Features

- The same line editing as Bash (from the [GNU Readline library](https://tiswww.case.edu/php/chet/readline/rltop.html) version 8.1).
- History persistence between sessions.
- Context sensitive completion;
  - Executables (and aliases).
  - Directory commands.
  - Environment variables.
- Context sensitive colored input text.
- New keyboard shortcuts;
  - Paste from clipboard (<kbd>Ctrl</kbd>+<kbd>V</kbd>).
  - Incremental history search (<kbd>Ctrl</kbd>+<kbd>R</kbd> and <kbd>Ctrl</kbd>+<kbd>S</kbd>).
  - Powerful completion (<kbd>Tab</kbd>).
  - Undo (<kbd>Ctrl</kbd>+<kbd>Z</kbd>).
  - Automatic `cd ..` (<kbd>Ctrl</kbd>+<kbd>PgUp</kbd>).
  - Environment variable expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>E</kbd>).
  - Doskey alias expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd>).
  - Scroll the screen buffer (<kbd>Alt</kbd>+<kbd>Up</kbd>, etc).
  - (press <kbd>Alt</kbd>+<kbd>H</kbd> for many more...)
- Directory shortcuts;
  - Typing a directory name followed by a path separator is a shortcut for `cd /d` to that directory.
  - Typing `..` or `...` is a shortcut for `cd ..` or `cd ..\..` (each additional `.` adds another `\..`).
  - Typing `-` or `cd -` changes to the previous current working directory.
- Scriptable completion with Lua.
- Colored and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds <kbd>Alt</kbd>+<kbd>H</kbd> to display the current key bindings. More features can also be found in GNU's [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html) and [History](https://tiswww.cwru.edu/php/chet/readline/history.html) libraries' manuals.

# Usage

There are several ways to start Clink.

1. If you installed the auto-run, just start `cmd.exe`. Run `clink autorun --help` for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `<install_dir>\clink.exe inject`.

# Upgrading from Clink v0.4.9

The new Clink tries to be as backward compatible with Clink v0.4.9 as possible. However, in some cases upgrading may require a little bit of configuration work.

- Some key binding sequences have changed; see [Key Bindings](#keybindings) for more information.
- Match coloring is now done by Readline and is configured differently; see [Completion Colors](#completioncolors) for more information.
- Settings and history should migrate automatically if the new `clink_settings` and `clink_history` files don't exist (deleting them will cause migration to happen again).  To find the directory that contains these files, run `clink info` and look for the "state" line.
- Script compatibility should be very good, but some scripts may still encounter problems.  If you do encounter a compatibility problem you can look for an updated version of the script, update the script yourself, or visit the <a href="https://github.com/chrisant996/clink/issues">repo</a> and open an issue describing details about the compatibility problem.
- Some settings have changed slightly, and there are many new settings.  See [Configuring Clink](#configclink) for more information.

# How Clink Works

When running Clink via the methods above, Clink checks the parent process is supported and injects a DLL into it. The DLL then hooks the WriteConsole() and ReadConsole() Windows functions. The former is so that Clink can capture the current prompt, and the latter hook allows Clink to provide its own Readline-powered command line editing.

<a name="configclink"/>

# Configuring Clink

The easiest way to configure Clink is to use Clink's `set` command line option.  This can list, query, and set Clink's settings. Run `clink set --help` from a Clink-installed cmd.exe process to learn more both about how to use it and to get descriptions for Clink's various options.

The following table describes the available Clink settings:

Name                         | Default | Description
:--:                         | :-:     | -----------
`clink.colorize_input`       | True    | Enables context sensitive coloring for the input text (see <a href="#classifywords">Coloring The Input Text</a>).
`clink.paste_crlf`           | `space` | What to do with CR and LF characters on paste. Set this to `delete` to delete them, or to `space` to replace them with spaces.
`clink.path`                 |         | A list of paths to load Lua scripts. Multiple paths can be delimited semicolons.
`clink.promptfilter`         | True    | Enable prompt filtering by Lua scripts.
`cmd.auto_answer`            | `off`   | Automatically answers cmd.exe's "Terminate batch job (Y/N)?" prompts. `off` = disabled, `answer_yes` = answer Y, `answer_no` = answer N.
`cmd.ctrld_exits`            | True    | <kbd>Ctrl</kbd>+<kbd>D</kbd> exits the process when it is pressed on an empty line.
`color.arg`                  |         | The color for arguments in the input line when `clink.colorize_input` is enabled.
<a name="color_cmd"/>`color.cmd` | `bold` | Used when Clink displays shell (CMD.EXE) command completions, and in the input line when `clink.colorize_input` is enabled.
<a name="color_doskey"/>`color.doskey` | `bright cyan` | Used when Clink displays doskey alias completions, and in the input line when `clink.colorize_input` is enabled.
`color.filtered`             | `bold`  | The default color for filtered completions (see <a href="#filteringthematchdisplay">Filtering the Match Display</a>).
`color.flag`                 | `default` | The color for flags in the input line when `clink.colorize_input` is enabled.
<a name="color_hidden"/>`color.hidden` | | Used when Clink displays file completions with the "hidden" attribute.
`color.horizscroll`          |         | Used when Clink displays the `<` or `>` horizontal scroll indicators when Readline's `horizontal-scroll-mode` variable is set.
`color.input`                |         | Used when Clink displays the input line text. Note that when `clink.colorize_input` is disabled, the entire input line is displayed using `color.input`.
`color.interact`             | `bold`  | Used when Clink displays text or prompts such as a pager's `--More?--` prompt.
`color.message`              | `default` | The color for the message area (e.g. the search prompt message, digit argument prompt message, etc).
`color.modmark`              |         | Used when Clink displays the `*` mark on modified history lines when Readline's `mark-modified-lines` variable and Clink's `color.input` setting are both set. Falls back to `color.input` if not set.
`color.prompt`               |         | When set, this is used as the default color for the prompt.  But it's overridden by any colors set by <a href="#customisingtheprompt">Customising The Prompt</a>.
<a name="color_readonly"/>`color.readonly` | | Used when Clink displays file completions with the "readonly" attribute.
`color.unexpected`           | `default` | The color for unexpected arguments in the input line when `clink.colorize_input` is enabled.
`doskey.enhanced`            | True    | Enhanced Doskey adds the expansion of macros that follow `\|` and `&` command separators and respects quotes around words when parsing `$1`..`$9` tags. Note that these features do not apply to Doskey use in Batch files.
`exec.cwd`                   | True    | When matching executables as the first word (`exec.enable`), include executables in the current directory. (This is implicit if the word being completed is a relative path).
`exec.dirs`                  | True    | When matching executables as the first word (`exec.enable`), also include directories relative to the current working directory as matches.
`exec.enable`                | True    | Match executables when completing the first word of a line.
`exec.path`                  | True    | When matching executables as the first word (`exec.enable`), include executables found in the directories specified in the `%PATH%` environment variable.
`exec.space_prefix`          | True    | If the line begins with whitespace then Clink bypasses executable matching (`exec.path`) and will do normal files matching instead.
`files.hidden`               | True    | Includes or excludes files with the "hidden" attribute set when generating file lists.
`files.system`               | False   | Includes or excludes files with the "system" attribute set when generating file lists.
`history.dont_add_to_history_cmds` | `exit history` | List of commands that aren't automatically added to the history. Commands are separated by spaces, commas, or semicolons. Default is `exit history`, to exclude both of those commands.
`history.dupe_mode`          | `erase_prev` | If a line is a duplicate of an existing history entry Clink will erase the duplicate when this is set `erase_prev`. Setting it to `ignore` will not add duplicates to the history, and setting it to `add` will always add lines.
`history.expand_mode`        | `not_quoted` | The `!` character in an entered line can be interpreted to introduce words from the history. This can be enabled and disable by setting this value to `on` or `off`. Values of `not_squoted`, `not_dquoted`, or `not_quoted` will skip any `!` character quoted in single, double, or both quotes respectively.
`history.ignore_space`       | True    | Ignore lines that begin with whitespace when adding lines in to the history.
`history.max_lines`          | 2500    | The number of history lines to save if `history.save` is enabled (1 to 50000).
`history.save`               | True    | Saves history between sessions.
`history.shared`             | False   | When history is shared, all instances of Clink update the master history list after each command and reload the master history list on each prompt.  When history is not shared, each instance updates the master history list on exit.
`lua.break_on_error`         | False   | Breaks into Lua debugger on Lua errors.
`lua.break_on_traceback`     | False   | Breaks into Lua debugger on `traceback()`.
`lua.debug`                  | False   | Loads a simple embedded command line debugger when enabled. Breakpoints can be added by calling `pause()`.
`lua.path`                   |         | Value to append to `package.path`. Used to search for Lua scripts specified in `require()` statements.
<a name="lua_reload_scripts"/>`lua.reload_scripts` | False | When false, Lua scripts are loaded once and are only reloaded if forced (see <a href="#lua-scripts-location">The Location of Lua Scripts</a> for details).  When true, Lua scripts are loaded each time the edit prompt is activated.
`lua.traceback_on_error`     | False   | Prints stack trace on Lua errors.
`match.ignore_accent`        | True    | Controls accent sensitivity when completing matches. For example, `ä` and `a` are considered equivalent with this enabled.
`match.ignore_case`          | `relaxed` | Controls case sensitivity when completing matches. `off` = case sensitive, `on` = case insensitive, `relaxed` = case insensitive plus `-` and `_` are considered equal.
`match.sort_dirs`            | `with`  | How to sort matching directory names. `before` = before files, `with` = with files, `after` = after files.
`match.wild`                 | True    | Matches `?` and `*` wildcards when using any of the `menu-complete` commands. Turn this off to behave how bash does.
`readline.hide_stderr`       | False   | Suppresses stderr from the Readline library.  Enable this if Readline error messages are getting in the way.
`terminal.emulation`         | `auto`  | Clink can either emulate a virtual terminal and handle ANSI escape codes itself, or let the console host natively handle ANSI escape codes. `native` = pass output directly to the console host process, `emulate` = clink handles ANSI escape codes itself, `auto` = emulate except when running in ConEmu.
`terminal.modify_other_keys` | True    | When enabled, pressing <kbd>Space</kbd> or <kbd>Tab</kbd> with modifier keys sends extended XTerm key sequences so they can be bound separately.

<p/>

> **Compatibility Notes:**
> - The `esc_clears_line` setting has been replaced by a `clink-reset-line` command that is by default bound to the <kbd>Escape</kbd> key.  See [Key Bindings](#keybindings) and [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html) for more information.
> - The `use_altgr_substitute` setting has been removed.  If <kbd>AltGr</kbd> or lack of <kbd>AltGr</kbd> causes a problem, please visit the <a href="https://github.com/chrisant996/clink/issues">repo</a> and open an issue with details describing the problem.
> - The `match_colour` setting has been removed, and Clink now supports Readline's completion coloring.  See [Completion Colors](#completioncolors) for more information.

<a name="colorsettings"/>

## Color Settings

### Friendly Color Names

The Clink color settings use the following syntax:

<code>[<span class="arg">attributes</span>] [<span class="arg">foreground_color</span>] [on [<span class="arg">background_color</span>]]</code>

Optional attributes (can be abbreviated to 3 letters):
- `bold` or `dim` adds or removes brightness (high intensity) to the default foreground color (if the default color is bright white, then `dim` uses normal white).
- `underline` or `nounderline` adds or removes an underline.

Optional colors for <span class="arg">foreground_color</span> and <span class="arg">background_color</span> (can be abbreviated to 3 letters):
- `default` or `normal` uses the default color as defined by the current color theme in the console window.
- `black`, `red`, `green`, `yellow`, `blue`, `cyan`, `magenta`, `white` are the basic colors names.
- `bright` can be combined with any of the other color names to make them bright (high intensity).

Examples (specific results may depend on the console host program):
- `bri yel` for bright yellow foreground on default background color.
- `bold` for bright default foreground on default background color.
- `underline bright black on white` for dark gray (bright black) foreground on light gray (white) background.
- `default on blue` for default foreground color on blue background.

### Alternative SGR Syntax

It's also possible to set any ANSI <a href="https://en.wikipedia.org/wiki/ANSI_escape_code#SGR">SGR escape code</a> using <code>sgr <span class="arg">SGR_parameters</span></code> (for example `sgr 7` is the code for reverse video, which swaps the foreground and background colors).

Be careful, since some escape code sequences might behave strangely.

<a name="filelocations"/>

## File Locations

Settings and history are persisted to disk from session to session. The location of these files depends on which distribution of Clink was used. If you installed Clink using the .exe installer then Clink uses the current user's non-roaming application data directory. This user directory is usually found in one of the following locations;

- Windows XP: `c:\Documents and Settings\<username>\Local Settings\Application Data\clink`
- Windows Vista onwards: `c:\Users\<username>\AppData\Local\clink`

All of the above locations can be overridden using the <code>--profile <span class="arg">path</span></code> command line option which is specified when injecting Clink into cmd.exe using `clink inject`.

## Command Line Options

<p>
<dt>clink</dt>
<dd>
Shows command line usage help.</dd>
</p>

<p>
<dt>clink inject</dt>
<dd>
Injects Clink into a CMD.EXE process.<br/>
See <code>clink inject --help</code> for more information.</dd>
</p>

<p>
<dt>clink autorun</dt>
<dd>
Manages Clink's entry in CMD.EXE's autorun section, which can automatically inject Clink when starting CMD.EXE.<br/>
See <code>clink autorun --help</code> for more information.</dd>
</p>

<p>
<dt>clink set</dt>
<dd>
<code>clink set</code> by itself lists all settings and their values.<br/>
<code>clink set <span class="arg">setting_name</span></code> describes the setting shows its current value.<br/>
<code>clink set <span class="arg">setting_name</span> clear</code> resets the setting to its default value.<br/>
<code>clink set <span class="arg">setting_name</span> <span class="arg">value</span></code> sets the setting to the specified value.</dd>
</p>

<p>
<dt>clink history</dt>
<dd>
Lists the command history.<br/>
See <code>clink history --help</code> for more information.<br/>
Also, Clink automatically defines <code>history</code> as an alias for <code>clink history</code>.</dd>
</p>

<p>
<dt>clink info</dt>
<dd>
Prints information about Clink, including the version and various configuration directories and files.<br/>
Or <code>clink --version</code> shows just the version number.</dd>
</p>

<p>
<dt>clink echo</dt>
<dd>
Echos key sequences to use in the inputrc files for binding keys to Clink commands.  Each key pressed prints the associated key sequence on a separate line, until <kbd>Ctrl</kbd>+<kbd>C</kbd> is pressed.</dd>
</p>

# Configuring Readline

Readline itself can also be configured to add custom keybindings and macros by creating a Readline init file. There is excellent documentation for all the options available to configure Readline in Readline's [manual](https://tiswww.cwru.edu/php/chet/readline/rltop.html#Documentation).

Clink searches in the directories referenced by the following environment variables and loads any `.inputrc` or `_inputrc` files present, in the order listed here:
- `%CLINK_INPUTRC%`
- `%USERPROFILE%`
- `%LOCALAPPDATA%`
- `%APPDATA%`
- `%HOME%`

Configuration in files loaded earlier can be overridden by files loaded later.

Other software that also uses Readline will also look for the `.inputrc` file (and possibly the `_inputrc` file too). To set macros and keybindings intended only for Clink one can use the Readline init file conditional construct like this; `$if clink [...] $endif`.

> **Compatibility Notes:**
> - The `clink_inputrc_base` file from v0.4.8 is no longer used.
> - For backward compatibility, `clink_inputrc` is also loaded from the above locations, but it has been deprecated and may be removed in the future.

## Basic Format

The quick version is that mostly you'll use these kinds of lines:
- <code><em>keyname</em>: <em>command</em></code>
- <code><em>keyname</em>: "<em>literal text</em>"</code>
- <code>set <em>varname</em> <em>value</em></code>

Refer to the Readline [manual](https://tiswww.cwru.edu/php/chet/readline/rltop.html#Documentation) for a more thorough explanation of the inputrc file format, list of available commands, and list of configuration variables and their values.

See [Key Bindings](#keybindings) for more information about binding keys in Clink.

## New Configuration Variables

Clink adds some new configuration variables to Readline, beyond what's described in the Readline manual:

Name | Default | Description
:-:|:-:|---
`completion-auto-query-items`|on|Automatically prompts before displaying completions if they need more than half a screen page.
`history-point-at-end-of-anchored-search`|off|Puts the cursor at the end of the line when using `history-search-forward` or `history-search-backward`.
`search-ignore-case`|off|Controls whether the history search commands ignore case.

## New Commands

Clink adds some new commands to Readline, beyond what's described in the Readline manual:

Name | Description
:-:|---
`add-history`|Adds the current line to the history without executing it, and clears the editing line.
`clink-copy-cwd`|Copy the current working directory to the clipboard.
`clink-copy-line`|Copy the current line to the clipboard.
`clink-copy-word`|Copy the word at the cursor to the clipboard.
`clink-ctrl-c`|Discards the current line and starts a new one (like <kbd>Ctrl</kbd>+<kbd>C</kbd> in CMD.EXE).
`clink-exit`|Replaces the current line with `exit` and executes it (exits the shell instance).
`clink-expand-doskey-alias`|Expand the doskey alias (if any) at the beginning of the line.
`clink-expand-env-vars`|Expand the environment variable (e.g. `%FOOBAR%`) at the cursor.
`clink-insert-dot-dot`|Inserts `..\` at the cursor.
`clink-paste`|Paste the clipboard at the cursor.
`clink-popup-complete`|Show a popup window that lists the available completions.
`clink-popup-directories`|Show a popup window of recent current working directories.  In the popup, use <kbd>Enter</kbd> to `cd /d` to the highlighted directory.  See below more about the popup window.
`clink-popup-history`|Show a popup window that lists the command history (if any text precedes the cursor then it uses an anchored search to filter the list).  In the popup, use <kbd>Enter</kbd> to execute the highlighted command.  See below for more about the popup window.
`clink-reload`|Reloads the inputrc file and the Lua scripts.
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

<a name="completioncolors"/>

## Completion Colors

When `colored-completion-prefix` is configured to `on`, then the "so" color from `%LS_COLORS%` is used to color the common prefix when displaying possible completions.  The default for "so" is magenta, but for example `set LS_COLORS=so=90` sets the color to bright black (which shows up as a dark gray).

When `colored-stats` is configured to `on`, then the color definitions from `%LS_COLORS%` (using ANSI escape codes) are used to color file completions according to their file type or extension.  Each definition is a either a two character type id or a file extension, followed by an equals sign and then the [SGR parameters](https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_parameters) for an ANSI escape code.  Multiple definitions are separated by colons.  Also, since `%LS_COLORS%` doesn't cover readonly files, hidden files, doskey aliases, or shell commands the `color.readonly`, `color.hidden`, `color.doskey`, and `color.cmd` [Clink settings](#configclink) exist to cover those.

Here is an example where `%LS_COLORS%` defines colors for various types.

- `so=` defines the color for the common prefix for possible completions.
- `fi=` defines the color for normal files.
- `di=` defines the color for directories.
- `ex=` defines the color for executable files.

```plaintext
set LS_COLORS=so=90:fi=97:di=93:ex=92:*.pdf=30;105:*.md=4
```

Let's break that down:

- `so=90` uses bright black (dark gray) for the common prefix for possible completions.
- `fi=97` uses bright white for files.
- `di=93` uses bright yellow for directories.
- `ex=92` uses bright green for executable files.
- `*.pdf=30;105` uses black on bright magenta for .pdf files.
- `*.md=4` uses underline for .md files.

## Popup window

The `clink-popup-complete`, `clink-popup-directories`, and `clink-popup-history` commands show a popup window that lists the available completions, directory history, or command history.  Here's how it works:

Key | Description
:-:|---
<kbd>Escape</kbd>|Cancels the popup.
<kbd>Enter</kbd>|Inserts the highlighted completion, changes to the highlighted directory, or executes the highlighted command.
<kbd>Shift</kbd>+<kbd>Enter</kbd>|Inserts the highlighted completion, inserts the highlighted directory, or jumps to the highlighted command history entry without executing it.
<kbd>Ctrl</kbd>+<kbd>Enter</kbd>|Same as <kbd>Shift</kbd>+<kbd>Enter</kbd>.
Typing|Typing does an incremental search.
<kbd>F3</kbd>|Go to the next match.
<kbd>Ctrl</kbd>+<kbd>L</kbd>|Go to the next match.
<kbd>Shift</kbd>+<kbd>F3</kbd>|Go to the previous match.
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd>|Go to the previous match.

# Extending Clink

The Readline library allows clients to offer an alternative path for creating completion matches. Clink uses this to hook Lua into the completion process making it possible to script the generation of matches with <a href="https://www.lua.org/docs.html">Lua</a> scripts. The following sections describe this in more detail and show some examples.

<a name="lua-scripts-location"/>

## The Location of Lua Scripts

Clink loads all Lua scripts it finds in these directories:
1. All directories listed in the `clink.path` setting, separated by semicolons.
2. If `clink.path` is not set, then the DLL directory and the profile directory are used (see <a href="#filelocations">File Locations</a> for info about the profile directory).
3. All directories listed in the `%CLINK_PATH%` environment variable, separated by semicolons.

Lua scripts are loaded once and are only reloaded if forced because the scripts locations change, the `clink-reload` command is invoked (<kbd>Ctrl</kbd>+<kbd>X</kbd>,<kbd>Ctrl</kbd>+<kbd>R</kbd>), or the `lua.reload_scripts` setting changes (or is True).

<a name="matchgenerators"/>

## Match Generators

These are Lua functions that are called as part of Readline's completion process.

First create a match generator object:

```lua
local my_generator = clink.generator(priority)
```

The <span class="arg">priority</span> argument is a number that influences when the generator gets called, with lower numbers going before higher numbers.

### The :generate() Function

Next define a match generator function on the object, taking the following form:

```lua
function my_generator:generate(line_state, match_builder)
    -- Use the line_state object to examine the current line and create matches.
    -- Submit matches to Clink using the match_builder object.
    -- Return true or false.
end
```

<span class="arg">line_state</span> is a <a href="#line">line</a> object that has information about the current line.

<span class="arg">match_builder</span> is a <a href="#builder">builder</a> object to which matches can be added.

If no further match generators need to be called then the function should return true.  Returning false or nil continues letting other match generators get called.

Here is an example script that supplies git branch names as matches for `git checkout`.  It's based on git_branch_autocomplete.lua from [collink.clink-git-extensions](https://github.com/collink/clink-git-extensions).  The version here is updated for the new Clink Lua API, and for illustration purposes it's been simplified to not support git aliases.

```lua
local git_branch_autocomplete = clink.generator(1)

local function string.starts(str, start)
    return string.sub(str, 1, string.len(start)) == start
end

local function is_checkout_ac(text)
    if string.starts(text, "git checkout") then
        return true
    end
    return false
end

local function get_branches()
    -- Run git command to get branches.
    local handle = io.popen("git branch -a 2>&1")
    local result = handle:read("*a")
    handle:close()
    -- Parse the branches from the output.
    local branches = {}
    if string.starts(result, "fatal") == false then
        for branch in string.gmatch(result, "  %S+") do
            branch = string.gsub(branch, "  ", "")
            if branch ~= "HEAD" then
                table.insert(branches, branch)
            end
        end
    end
    return branches
end

function git_branch_autocomplete:generate(line_state, match_builder)
    -- Check if it's a checkout command.
    if not is_checkout_ac(line_state:getline()) then
        return false
    end
    -- Get branches and add them (does nothing if not in a git repo).
    local matchCount = 0
    for _, branch in ipairs(get_branches()) do
        match_builder:addmatch(branch)
        matchCount = matchCount + 1
    end
    -- If we found branches, then stop other match generators.
    return matchCount > 0
end
```

### The :getwordbreakinfo() Function

A generator can influence word breaking for the end word by defining a `:getwordbreakinfo()` function.  The function takes a <span class="arg">line_state</span> <a href="#line">line</a> object that has information about the current line.  If it returns nil or 0, the end word is truncated to 0 length.  This is the normal behavior, which allows Clink to collect and cache all matches and then filter them based on typing.  Or it can return two numbers:  word break length and an optional end word length.  The end word is split at the word break length:  one word contains the first word break length characters from the end word (if 0 length then it's discarded), and the next word contains the rest of the end word truncated to the optional word length (0 if omitted).

For example, when the environment variable match generator sees the end word is `abc%USER` it returns `3,1` so that the last two words become "abc" and "%" so that its generator knows it can do environment variable matching.  But when it sees `abc%FOO%def` it returns `8,0` so that the last two words become "abc%FOO%" and "" so that its generator won't do environment variable matching, and also so other generators can produce matches for what follows, since "%FOO%" is an already-completed environment variable and therefore should behave like a word break.  In other words, it breaks the end word differently depending on whether the number of percent signs is odd or even, to account for environent variable syntax rules.

And when an argmatcher sees the end word begins with a flag character it returns `0,1` so the end word contains only the flag character in order to switch from argument matching to flag matching.

> **Note:** The `:getwordbreakinfo()` function is called very often, so it needs to be very fast or it can cause responsiveness problems while typing.

```lua
local envvar_generator = clink.generator(10)

function envvar_generator:generate(line_state, match_builder)
    -- Does the word end with a percent sign?
    local word = line_state:getendword()
    if word:sub(-1) ~= "%" then
        return false
    end

    -- Add env vars as matches.
    for _, i in ipairs(os.getenvnames()) do
        match_builder:addmatch("%"..i.."%", "word")
    end

    match_builder:setsuppressappend()   -- Don't append a space character.
    match_builder:setsuppressquoting()  -- Don't quote envvars.
    return true
end

function envvar_generator:getwordbreakinfo(line_state)
    local word = line_state:getendword()
    local in_out = false
    local index = nil

    -- Paired percent signs denote already-completed environment variables.
    -- So use envvar completion for abc%foo%def%USER but not for abc%foo%USER.
    for i = 1, #word do
        if word:sub(i, i) == "%" then
            in_out = not in_out
            if in_out then
                index = i - 1
            else
                index = i
            end
        end
    end

    -- If there were any percent signs, return word break info to influence the
    -- match generators.
    if index then
        return index, (in_out and 1) or 0
    end
end
```

<a name="argumentcompletion"/>

## Argument Completion

Clink provides a framework for writing complex argument match generators in Lua.  It works by creating a parser object that describes a command's arguments and flags and then registering the parser with Clink. When Clink detects the command is being entered on the current command line being edited, it uses the parser to generate matches.

Here is an example of a simple parser for the command `foobar`;

```lua
clink.argmatcher("foobar")
:addflags("-foo", "-bar")
:addarg(
    { "hello", "hi" },      -- Completions for arg #1
    { "world", "wombles" }  -- Completions for arg #2
)
```

This parser describes a command that has two positional arguments each with two potential options. It also has two flags which the parser considers to be position independent meaning that provided the word being completed starts with a certain prefix the parser with attempt to match the from the set of flags.

On the command line completion would look something like this:

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black;color:#cccccc">C:&gt;foobar hello -foo wo
wombles  wonder   world
C:&gt;foobar hello -foo wo<span style="color:#ffffff">_</span>
</code></pre>

When displaying possible completions, flag matches are only shown if the flag character has been input (so `command ` and <kbd>Alt</kbd>+<kbd>=</kbd> would list only non-flag matches, or `command -` and <kbd>Alt</kbd>+<kbd>=</kbd> would list only flag matches).

### More Advanced Stuff

#### Linking Parsers

There are often situations where the parsing of a command's arguments is dependent on the previous words (`git merge ...` compared to `git log ...` for example). For these scenarios Clink allows you to link parsers to arguments' words using Lua's concatenation operator. Parsers can also be concatenated with flags too.

```lua
a_parser = clink.argmatcher():addarg({ "foo", "bar" })
b_parser = clink.argmatcher():addarg({ "abc", "123" })
c_parser = clink.argmatcher()
c_parser:addarg({ "foobar" .. a_parser })   -- Arg #1 is "foobar", which has args "foo" or "bar".
c_parser:addarg({ b_parser })               -- Arg #2 is "abc" or "123".
```

As the example above shows, it is also possible to use a parser without concatenating it to a word. When Clink follows a link to a parser it is permanent and it will not return to the previous parser.

#### Functions As Argument Options

Argument options are not limited solely to strings. Clink also accepts functions too so more context aware argument options can be used.

```lua
function rainbow_function(word)
    return { "red", "white", "blue" }
end

the_parser = clink.argmatcher()
the_parser:addarg({ "zippy", "bungle", "george" })
the_parser:addarg({ rainbow_function, "yellow", "green" })
```

The functions take a single argument which is a word from the command line being edited (or partial word if it is the one under the cursor). Functions should return a table of potential matches.

Some built-in matcher functions are available:

Function | Description
:-: | ---
<a href="#clink.dirmatches">clink.dirmatches</a> | Generates directory matches.
<a href="#clink.filematches">clink.filematches</a> | Generates file matches.

#### Shorthand

It is also possible to omit the `addarg` and `addflags` function calls and use a more declarative shorthand form:

```lua
-- Shorthand form; requires tables.
clink.argmatcher()
    { "one", "won" }                -- Arg #1
    { "two", "too" }                -- Arg #2
    { "-a", "-b", "/?", "/h" }      -- Flags

-- Normal form:
clink.argmatcher()
:addarg(
    { "one", "won" }                -- Arg #1
    { "two", "too" }                -- Arg #2
)
:addflags("-a", "-b", "/?", "/h")   -- Flags
```

With the shorthand form flags are implied rather than declared.  When a shorthand table's first value is a string starting with `-` or `/` then the table is interpreted as flags.  Note that it's still possible with shorthand form to mix flag prefixes, and even add additional flag prefixes, such as <code>{ <span class="hljs-string">'-a'</span>, <span class="hljs-string">'/b'</span>, <span class="hljs-string">'=c'</span> }</code>.

<a name="classifywords"/>

#### Coloring The Input Text

When the `clink.colorize_input` setting is enabled, argmatchers apply colors to the input text by parsing it.

In cases where an argmatcher isn't able to color the input text in the desired manner, it's also possible to supply a classifier function that can override how the argmatcher would color the input text.  An argmatcher's classifier function is called once for each word the argmatcher parses, but it can classify any words (not just the word it was called for).  Each argmatcher can have its own function, so when there are linked argmatchers more than one function may be invoked.

Words are colored by classifying the words, and each classification has an associated color.

Code | Classification | Color
---|---|---
`"a"`|Argument; used for words that match a list of preset argument matches.|`color.arg` or `color.input`
`"c"`|Shell command; used for CMD command names.|`color.cmd`
`"d"`|Doskey alias.|`color.doskey`
`"f"`|Flag; used for flags that match a list of preset flag matches.|`color.flag`
`"o"`|Other; used for file names and words that don't fit any of the other classifications.|`color.input`
`"n"`|None; used for words that aren't recognized as part of the expected input syntax|`color.unexpected`

The `clink set` command has different syntax depending on the setting type, so the argmatcher for `clink` needs help in order to get everything right.  A custom generator function parses the input text to provide appropriate matches, and a custom classifier function classifies the words.

```lua
-- In this example, the argmatcher matches a directory as the first argument and
-- a file as the second argument.  It uses a word classifier function to classify
-- directories (words that end with a path separator) as "unexpected" in the
-- second argument position.

local function classify_handler(arg_index, word, word_index, line_state, classify)
    -- `arg_index` is the argument position in the argmatcher.
    -- In this example only position 2 needs special treatent.
    if arg_index ~= 2 then
        return
    end

    -- `arg_index` is the argument position in the argmatcher.
    -- `word_index` is the word position in the `line_state`.
    -- Ex1: in `samp dir file` for the word `dir` the argument index is 1 and
    -- the word index is 2.
    -- Ex2: in `samp --help dir file` for the word `dir` the argument index is
    -- still 1, but the word index is 3.

    -- `word` is the word the classifier function was called for and `word_index`
    -- is its position in the line.  Because `line_state` is also provided, the
    -- function can examine any words in the input line.
    if word:sub(-1) == "\\" then
        -- The word appears to be a directory, but this example expects only
        -- files are expected in argument position 2.  Here the word gets
        -- classified as "n" (unexpected) so it gets colored differently.
        classify:classifyword("n")
    end
end

local matcher = clink.argmatcher("samp")
:addflags("--help")
:addarg({ clink.dirmatches })
:addarg({ clink.filematches })
:setclassifier(classify_handler)
```

<a name="filteringthematchdisplay"/>

#### Filtering The Match Display

In some instances it may be preferable to display potential matches in an alternative form than the generated matches passed to and used internally by Readline. For example, it might be desirable to display a `*` next to some matches, or to show additional information about each match. Filtering the match display only affects what is displayed; it doesn't affect completing matches.

A match generator can use <a href="#clink.ondisplaymatches">clink.ondisplaymatches()</a> to register a function that will be called before matches are displayed (this is reset every time match generation is invoked).

The function receives a table argument containing the matches to be displayed, and a boolean argument indicating whether they'll be displayed in a popup window. The table argument has a `match` string field and a `type` string field; these are the same as in <a href="builder:addmatch">builder:addmatch()</a>. The return value is a table with the input matches filtered as required by the match generator. The returned table can also optionally include a `display` string field and a `description` string field. When present, `display` will be displayed as the match instead of the `match` field, and `description` will be displayed next to the match. Putting the description in a separate field enables Clink to align the descriptions in a column.

```lua
local function my_filter(matches, popup)
    local new_matches = {}
    for _,m in ipairs(matches) do
        if m.match:find("[0-9]") then
            -- Ignore matches with one or more digits.
        else
            -- Keep the match, and also add * prefix to directory matches.
            if m.type:find("^dir") then
                m.display = "*"..m.match
            end
            table.insert(new_matches, m)
        end
    end
    return new_matches
end

function my_match_generator:generate(line_state, match_builder)
    ...
    clink.ondisplaymatches(my_filter)
end
```

<p/>

> **Compatibility Note:**  When a match display filter has been set, it changes how match generation behaves.
> - When a match display filter is set, then match generation is also re-run whenever matches are displayed.
> - Normally match generation only happens at the start of a new word.  The full set of potential matches is remembered and dynamically filtered based on further typing.
> - So if a match generator made contextual decisions during match generation (other than filtering) then it could potentially behave differently in Clink v1.x than it did in v0.x.

<a name="customisingtheprompt"/>

## Customising The Prompt

Before Clink displays the prompt it filters the prompt through Lua so that the prompt can be customised. This happens each and every time that the prompt is shown which allows for context sensitive customisations (such as showing the current branch of a git repository).

Writing a prompt filter is straightforward:
1. Create a new prompt filter by calling `clink.promptfilter()` along with a priority id which dictates the order in which filters are called. Lower priority ids are called first.
2. Define a `filter()` function on the returned prompt filter.

The filter function takes a string argument that contains the filtered prompt so far.  If the filter function returns nil, it has no effect.  If the filter function returns a string, that string is used as the new filtered prompt (and may be further modified by other prompt filters with higher priority ids).  If the filter function returns a string and a boolean, then if the boolean is false the prompt filtering is done and no further filter functions are called.

```lua
local p = clink.promptfilter(30)
function p:filter(prompt)
    return "new prefix "..prompt.." new suffix" -- Add ,false to stop filtering.
end
```

The following example illustrates setting the prompt, modifying the prompt, using ANSI escape code for colors, running a git command to find the current branch, and stopping any further processing.

```lua
local green  = "\x1b[92m"
local yellow = "\x1b[93m"
local cyan   = "\x1b[36m"
local normal = "\x1b[m"

-- A prompt filter that discards any prompt so far and sets the
-- prompt to the current working directory.  An ANSI escape code
-- colors it yellow.
local cwd_prompt = clink.promptfilter(30)
function cwd_prompt:filter(prompt)
    return yellow..os.getcwd()..normal
end

-- A prompt filter that inserts the date at the beginning of the
-- the prompt.  An ANSI escape code colors the date green.
local date_prompt = clink.promptfilter(40)
function date_prompt:filter(prompt)
    return green..os.date("%a %H:%M")..normal.." "..prompt
end

-- A prompt filter that may stop further prompt filtering.
-- This is a silly example, but on Wednesdays, it stops the
-- filtering, which in this example prevents git branch
-- detection and the line feed and angle bracket.
local wednesday_silliness = clink.promptfilter(45)
function wednesday_silliness:filter(prompt)
    if os.date("%a") == "Wed" then
        -- The ,false stops any further filtering.
        return prompt.." HAPPY HUMP DAY! ", false
    end
end

-- A prompt filter that appends the current git branch.
local git_branch_prompt = clink.promptfilter(50)
function git_branch_prompt:filter(prompt)
    for line in io.popen("git branch 2>nul"):lines() do
        local branch = line:match("%* (.+)$")
        if branch then
            return prompt.." "..cyan.."["..branch.."]"..normal
        end
    end
end

-- A prompt filter that adds a line feed and angle bracket.
local bracket_prompt = clink.promptfilter(150)
function bracket_prompt:filter(prompt)
    return prompt.."\n> "
end
```

The resulting prompt will look like this:

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black"><span style="color:#00ff00">Wed 12:54</span> <span style="color:#ffff00">c:\dir</span> <span style="color:#008080">[master]</span>
<span style="color:#cccccc">&gt;&nbsp;_</span>
</code></pre>

...except on Wednesdays, when it will look like this:

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black"><span style="color:#00ff00">Wed 12:54</span> <span style="color:#ffff00">c:\dir</span> <span style="color:#cccccc">HAPPY HUMP DAY!&nbsp;_</span>
</code></pre>

# Miscellaneous

<a name="keybindings"/>

## Key bindings

Key bindings are defined in the inputrc files.  See the [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html) manual for more information about the inputrc files (look for "Readline Init File").

Here is the quick version:

- A key binding is <code><em>name</em>: <em>function</em></code> or <code><em>name</em>: "<em>literal text</em>"</code>.
- Key names are like this:
  - `C-a` and `"\C-a"` are both <kbd>Ctrl</kbd>+<kbd>a</kbd>.
  - `M-a` and `"\M-a"` are both <kbd>Alt</kbd>+<kbd>a</kbd>.
  - `M-C-a` and `"\M-\C-a"` are both <kbd>Alt</kbd>+<kbd>Ctrl</kbd>+<kbd>a</kbd>.
  - `hello` is just <kbd>h</kbd>; the `ello` is a syntax error and is silently discarded by Readline.
  - `"hello"` is the series of keys <kbd>h</kbd>,<kbd>e</kbd>,<kbd>l</kbd>,<kbd>l</kbd>,<kbd>o</kbd>.
  - Special keys like <kbd>Up</kbd> are represented by VT220 escape codes such as`"\e[A"` (see table below for more info).
- Key bindings can be either functions or macros (literal text):
  - `blah-blah` binds to a function named "blah-blah".
  - `"blah-blah"` inserts the literal text "blah-blah".

Here is an example `.inputrc` file with the key bindings that I use myself:

<pre><code class="plaintext"><span class="hljs-meta">$if clink</span>           <span class="hljs-comment"># begin clink-only section</span>

<span class="hljs-comment"># The following key bindings are for emacs mode.</span>
<span class="hljs-meta">set keymap emacs</span>

<span class="hljs-comment"># Completion key bindings.</span>
<span class="hljs-string">"\t"</span>:               old-menu-complete               <span class="hljs-comment"># Tab</span>
<span class="hljs-string">"\e[Z"</span>:             old-menu-complete-backward      <span class="hljs-comment"># Shift+Tab</span>
<span class="hljs-string">"\e[27;5;9~"</span>:       clink-popup-complete            <span class="hljs-comment"># Ctrl+Tab (requires additional ConEmu configuration)</span>
<span class="hljs-string">"\x1b[27;5;32~"</span>:    complete                        <span class="hljs-comment"># Ctrl+Space</span>

<span class="hljs-comment"># Some key bindings I got used to from 4Dos/4NT/Take Command.</span>
C-b:                                                <span class="hljs-comment"># Ctrl+B (cleared because I redefined Ctrl+F)</span>
C-d:                remove-history                  <span class="hljs-comment"># Ctrl+D (replaces `delete-char`)</span>
C-f:                clink-expand-doskey-alias       <span class="hljs-comment"># Ctrl+F (replaces `forward-char`)</span>
C-k:                add-history                     <span class="hljs-comment"># Ctrl+K (replaces `kill-line`)</span>
<span class="hljs-string">"\e[A"</span>:             history-search-backward         <span class="hljs-comment"># Up (replaces `previous-history`)</span>
<span class="hljs-string">"\e[B"</span>:             history-search-forward          <span class="hljs-comment"># Down (replaces `next-history`)</span>
<span class="hljs-string">"\e[5~"</span>:            clink-popup-history             <span class="hljs-comment"># PgUp (replaces `history-search-backward`)</span>
<span class="hljs-string">"\e[6~"</span>:                                            <span class="hljs-comment"># PgDn (cleared because I redefined PgUp)</span>
<span class="hljs-string">"\e[1;5F"</span>:          end-of-line                     <span class="hljs-comment"># Ctrl+End (replaces `kill-line`)</span>
<span class="hljs-string">"\e[1;5H"</span>:          beginning-of-line               <span class="hljs-comment"># Ctrl+Home (replaces `backward-kill-line`)</span>

<span class="hljs-comment"># Some key bindings for interrogating the Readline configuration.</span>
<span class="hljs-string">"\C-x\C-f"</span>:         dump-functions                  <span class="hljs-comment"># Ctrl+X, Ctrl+F</span>
<span class="hljs-string">"\C-x\C-m"</span>:         dump-macros                     <span class="hljs-comment"># Ctrl+X, Ctrl+M</span>
<span class="hljs-string">"\C-x\C-v"</span>:         dump-variables                  <span class="hljs-comment"># Ctrl+X, Ctrl+V</span>

<span class="hljs-comment"># Misc other key bindings.</span>
<span class="hljs-string">"\e[5;6~"</span>:          clink-popup-directories         <span class="hljs-comment"># Ctrl+Shift+PgUp</span>
C-_:                kill-line                       <span class="hljs-comment"># Ctrl+- (replaces `undo`)</span>

<span class="hljs-meta">$endif</span>              <span class="hljs-comment"># end clink-only section</span>
</code></pre>

The `clink-show-help` command is bound to <kbd>Alt</kbd>+<kbd>H</kbd> and lists all currently active key bindings.  The list displays "friendly" key names, and these names are generally not suitable for use in inputrc files.  For example "Up" is the friendly name for `"\e[A"`, and "A-C-F2" is the friendly name for `"\e\e[1;5Q"`.  To see key sequence strings suitable for use an inputrc files use `clink echo` or <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd>.

> **Note:** Third party console hosts such as ConEmu may have their own key bindings that supersede Clink.  They usually have documentation for how to change or disable their key bindings to allow console programs to handle the keys instead.

### Discovering Clink key sequences

Clink provides an easy way to find the key sequence for any key combination that Clink supports. Run `clink echo` and then press key combinations; the associated key binding sequence is printed to the console output.

### Binding special keys

Here is a table of the key binding sequences for the special keys.  Clink primarily uses VT220 emulation for keyboard input, but also uses some Xterm extended key sequences.

|           |Normal     |Shift      |Ctrl         |Ctrl+Shift   |Alt       |Alt+Shift   |Alt+Ctrl     |Alt+Ctrl+Shift|
|:-:        |:-:        |:-:        |:-:          |:-:          |:-:       |:-:         |:-:          |:-:           |
|Up         |`\e[A`     |`\e[1;2A`  |`\e[1;5A`    |`\e[1;6A`    |`\e[1;3A` |`\e[1;4A`   |`\e[1;7A`    |`\e[1;8A`     |
|Down       |`\e[B`     |`\e[1;2B`  |`\e[1;5B`    |`\e[1;6B`    |`\e[1;3B` |`\e[1;4B`   |`\e[1;7B`    |`\e[1;8B`     |
|Left       |`\e[D`     |`\e[1;2D`  |`\e[1;5D`    |`\e[1;6D`    |`\e[1;3D` |`\e[1;4D`   |`\e[1;7D`    |`\e[1;8D`     |
|Right      |`\e[C`     |`\e[1;2C`  |`\e[1;5C`    |`\e[1;6C`    |`\e[1;3C` |`\e[1;4C`   |`\e[1;7C`    |`\e[1;8C`     |
|Insert     |`\e[2~`    |`\e[2;2~`  |`\e[2;5~`    |`\e[2;6~`    |`\e[2;3~` |`\e[2;4~`   |`\e[2;7~`    |`\e[2;8~`     |
|Delete     |`\e[3~`    |`\e[3;2~`  |`\e[3;5~`    |`\e[3;6~`    |`\e[3;3~` |`\e[3;4~`   |`\e[3;7~`    |`\e[3;8~`     |
|Home       |`\e[H`     |`\e[1;2H`  |`\e[1;5H`    |`\e[1;6H`    |`\e[1;3H` |`\e[1;4H`   |`\e[1;7H`    |`\e[1;8H`     |
|End        |`\e[F`     |`\e[1;2F`  |`\e[1;5F`    |`\e[1;6F`    |`\e[1;3F` |`\e[1;4F`   |`\e[1;7F`    |`\e[1;8F`     |
|PgUp       |`\e[5~`    |`\e[5;2~`  |`\e[5;5~`    |`\e[5;6~`    |`\e[5;3~` |`\e[5;4~`   |`\e[5;7~`    |`\e[5;8~`     |
|PgDn       |`\e[6~`    |`\e[6;2~`  |`\e[6;5~`    |`\e[6;6~`    |`\e[6;3~` |`\e[6;4~`   |`\e[6;7~`    |`\e[6;8~`     |
|Tab        |`\t`       |`\e[Z`     |`\e[27;5;9~` |`\e[27;6;9~` | -        | -          | -           | -            |
|Space      |`Space`    | -         |`\e[27;5;32~`|`\e[27;6;32~`| -        | -          |`\e[27;7;32~`|`\e[27;8;32~` |
|Backspace  |`^h`       | -         |`Rubout`     | -           |`\e^h`    | -          |`\eRubout`   | -            |
|Escape     |`\e[27;27~`| -         | -           | -           | -        | -          | -           | -            |
|F1         |`\eOP`     |`\e[1;2P`  |`\e[1;5P`    |`\e[1;6P`    |`\e\eOP`  |`\e\e[1;2P` |`\e\e[1;5P`  |`\e\e[1;6P`   |
|F2         |`\eOQ`     |`\e[1;2Q`  |`\e[1;5Q`    |`\e[1;6Q`    |`\e\eOQ`  |`\e\e[1;2Q` |`\e\e[1;5Q`  |`\e\e[1;6Q`   |
|F3         |`\eOR`     |`\e[1;2R`  |`\e[1;5R`    |`\e[1;6R`    |`\e\eOR`  |`\e\e[1;2R` |`\e\e[1;5R`  |`\e\e[1;6R`   |
|F4         |`\eOS`     |`\e[1;2S`  |`\e[1;5S`    |`\e[1;6S`    |`\e\eOS`  |`\e\e[1;2S` |`\e\e[1;5S`  |`\e\e[1;6S`   |
|F5         |`\e[15~`   |`\e[15;2~` |`\e[15;5~`   |`\e[15;6~`   |`\e\e[15~`|`\e\e[15;2~`|`\e\e[15;5~` |`\e\e[15;6~`  |
|F6         |`\e[17~`   |`\e[17;2~` |`\e[17;5~`   |`\e[17;6~`   |`\e\e[17~`|`\e\e[17;2~`|`\e\e[17;5~` |`\e\e[17;6~`  |
|F7         |`\e[18~`   |`\e[18;2~` |`\e[18;5~`   |`\e[18;6~`   |`\e\e[18~`|`\e\e[18;2~`|`\e\e[18;5~` |`\e\e[18;6~`  |
|F8         |`\e[19~`   |`\e[19;2~` |`\e[19;5~`   |`\e[19;6~`   |`\e\e[19~`|`\e\e[19;2~`|`\e\e[19;5~` |`\e\e[19;6~`  |
|F9         |`\e[20~`   |`\e[20;2~` |`\e[20;5~`   |`\e[20;6~`   |`\e\e[20~`|`\e\e[20;2~`|`\e\e[20;5~` |`\e\e[20;6~`  |
|F10        |`\e[21~`   |`\e[21;2~` |`\e[21;5~`   |`\e[21;6~`   |`\e\e[21~`|`\e\e[21;2~`|`\e\e[21;5~` |`\e\e[21;6~`  |
|F11        |`\e[23~`   |`\e[23;2~` |`\e[23;5~`   |`\e[23;6~`   |`\e\e[23~`|`\e\e[23;2~`|`\e\e[23;5~` |`\e\e[23;6~`  |
|F12        |`\e[24~`   |`\e[24;2~` |`\e[24;5~`   |`\e[24;6~`   |`\e\e[24~`|`\e\e[24;2~`|`\e\e[24;5~` |`\e\e[24;6~`  |

## Saved command history

Here's how the saved history works:

When the `history.saved` setting is enabled, then the command history is loaded and saved as follows (or when the setting is disabled, then it isn't saved between sessions).

Every time a new input line starts, Clink reloads the master history list and prunes it not to exceed the `history.max_lines` setting.

For performance reasons, deleting a history line marks the line as deleted without rewriting the history file.  When the number of deleted lines gets too large (exceeding the max lines or 200, which is larger) then the history file is compacted:  the file is rewritten with the deleted lines removed.

When the `history.shared` setting is enabled, then all instances of Clink update the master history file and reload it every time a new input line starts.  This gives the effect that all instances of Clink share the same history -- a command entered in one instance will appear in other instances' history the next time they start an input line.  When the setting is disabled, then each instance of Clink loads the master file but doesn't append its own history back to the master file until after it exits, giving the effect that once an instance starts its history is isolated from other instances' history.
