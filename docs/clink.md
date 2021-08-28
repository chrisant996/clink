# What is Clink?

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the well-known Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

### Features

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
  - Automatic `cd ..` (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>U</kbd>).
  - Environment variable expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>E</kbd>).
  - Doskey alias expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd>).
  - Scroll the screen buffer (<kbd>Alt</kbd>+<kbd>Up</kbd>, etc).
  - <kbd>Shift</kbd>+Arrow keys to select text, typing replaces selected text, etc.
  - (press <kbd>Alt</kbd>+<kbd>H</kbd> for many more...)
- Directory shortcuts;
  - Typing a directory name followed by a path separator is a shortcut for `cd /d` to that directory.
  - Typing `..` or `...` is a shortcut for `cd ..` or `cd ..\..` (each additional `.` adds another `\..`).
  - Typing `-` or `cd -` changes to the previous current working directory.
- Scriptable completion with Lua.
- Scriptable key bindings with Lua.
- Colored and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds <kbd>Alt</kbd>+<kbd>H</kbd> to display the current key bindings. More features can also be found in GNU's [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html) and [History](https://tiswww.cwru.edu/php/chet/readline/history.html) libraries' manuals.

<blockquote>
<p>
<strong>Want some quick but powerful tips to get started?</strong>
</p>
<p>
<table>
<tr><td><kbd>Ctrl</kbd>+<kbd>O</kbd></td><td>This is <code>operate-and-get-next</code>, which accepts the current input line and gets the next history line.  You can search history for a command, then press <kbd>Ctrl</kbd>+<kbd>O</kbd> to run that command and queue up the next command after it.  Repeat it to conveniently rerun a series of commands from the history.</td></tr>
<tr><td><kbd>Alt</kbd>+<kbd>.</kbd></td><td>This is <code>yank-last-arg</code>, which inserts the last argument from the previous line.  You can use it repeatedly to cycle backwards through the history, inserting the last argument from each line.  Learn more by reading up on the "yank" features in the Readline manual.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>R</kbd></td><td>This is <code>reverse-search-history</code>, which incrementally searches the history.  Press it, then type, and it does a reverse incremental search while you type.  Press <kbd>Ctrl</kbd>+<kbd>R</kbd> again (and again, etc) to search for other matches of the search text.  Learn more by reading up on the "search" and "history" features in the Readline manual.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>D</kbd></td><td>This is <code>remove-history</code>, which deletes the currently selected history line after using any of the history search or navigation commands.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>K</kbd></td><td>This is <code>add-history</code>, which adds the current line to the history without executing it, and then clears the input line.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>N</kbd></td><td>This is <code>clink-menu-complete-numbers</code>, which grabs numbers with 3 or more digits from the current console screen and cycles through inserting them as completions (binary, octal, decimal, hexadecimal).  Super handy for quickly inserting a commit hash that was printed as output from a preceding command.</td></tr>
<tr><td><kbd>Alt</kbd>+<kbd>0</kbd> to <kbd>Alt</kbd>+<kbd>9</kbd></td><td>These are <code>digit-argument</code>, which let you enter a numeric value used by many commands.  For example <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>W</kbd> copies the current word to the clipboard, but if you first type <kbd>Alt</kbd>+<kbd>2</kbd> followed by <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>W</kbd> then it copies the 3rd word to the clipboard (the first word is 0, the second is 1, etc).  Learn more by reading up on "Readline Arguments" in the Readline manual.</td></tr>
<tr><td><kbd>Alt</kbd>+<kbd>H</kbd></td><td>This is <code>clink-show-help</code>, which lists the key bindings and commands.  Learn more by visiting <a href="#keybindings">Key Bindings</a>.</td></tr>
</table>
</p>
</blockquote>

# Usage

There are several ways to start Clink.

1. If you installed the auto-run, just start `cmd.exe`. Run `clink autorun --help` for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `<install_dir>\clink.exe inject`.

### How Clink Works

When running Clink via the methods above, Clink checks the parent process is supported and injects a DLL into it. The DLL then hooks the WriteConsole() and ReadConsole() Windows functions. The former is so that Clink can capture the current prompt, and the latter hook allows Clink to provide its own Readline-powered command line editing.

<a name="privacy"></a>

### Privacy

Clink does not collect user data.  Clink writes diagnostic information to its local log file, and does not transmit the log file off the local computer.  For the location of the log file, refer to [File Locations](#filelocations) or run `clink info`.

<a name="configclink"></a>

# Configuring Clink

## Startup Cmd Script

When Clink is injected, it looks for a `clink_start.cmd` script in the binaries directory and [profile directory](#filelocations).  Clink automatically runs the scripts, if present, when the first CMD prompt is shown after Clink is injected.

You can set the `clink.autostart` setting to run a different command, or set it to "nul" to run no command at all.

<a name="clinksettings"></a>

## Clink Settings

The easiest way to configure Clink is to use Clink's `set` command line option.  This can list, query, and set Clink's settings. Run `clink set --help` from a Clink-installed cmd.exe process to learn more both about how to use it and to get descriptions for Clink's various options.

The following table describes the available Clink settings:

Name                         | Default | Description
:--:                         | :-:     | -----------
`clink.autostart`            |         | This command is automatically run when the first CMD prompt is shown after Clink is injected.  If this is blank (the default), then Clink instead looks for clink_start.cmd in the binaries directory and profile directory and runs them.  Set it to "nul" to not run any autostart command.
`clink.colorize_input`       | True    | Enables context sensitive coloring for the input text (see <a href="#classifywords">Coloring The Input Text</a>).
`clink.logo`                 | `full`  | Controls what startup logo to show when Clink is injected.  `full` = show full copyright logo (the default), `short` = show abbreviated version info, `none` = omit the logo.
`clink.paste_crlf`           | `crlf`  | What to do with CR and LF characters on paste. Setting this to `delete` deletes them, `space` replaces them with spaces, `ampersand` replaces them with ampersands, and `crlf` pastes them as-is (executing commands that end with a newline).
`clink.path`                 |         | A list of paths to load Lua scripts. Multiple paths can be delimited semicolons.
`clink.promptfilter`         | True    | Enable prompt filtering by Lua scripts.
`cmd.auto_answer`            | `off`   | Automatically answers cmd.exe's "Terminate batch job (Y/N)?" prompts. `off` = disabled, `answer_yes` = answer Y, `answer_no` = answer N.
`cmd.ctrld_exits`            | True    | <kbd>Ctrl</kbd>+<kbd>D</kbd> exits the process when it is pressed on an empty line.
`cmd.get_errorlevel`         | True    | When this is enabled, Clink runs a hidden `echo %errorlevel%` command before each interactive input prompt to retrieve the last exit code for use by Lua scripts.  If you experience problems, try turning this off.  This is on by default.
`color.arg`                  |         | The color for arguments in the input line when `clink.colorize_input` is enabled.
`color.argmatcher`           |         | The color for the command name in the input line when `clink.colorize_input` is enabled, if the command name has an argmatcher available.
<a name="color_cmd"></a>`color.cmd` | `bold` | Used when Clink displays shell (CMD.EXE) command completions, and in the input line when `clink.colorize_input` is enabled.
<a name="color_doskey"></a>`color.doskey` | `bright cyan` | Used when Clink displays doskey alias completions, and in the input line when `clink.colorize_input` is enabled.
<a name="color_filtered"></a>`color.filtered` | `bold` | The default color for filtered completions (see <a href="#filteringthematchdisplay">Filtering the Match Display</a>).
`color.flag`                 | `default` | The color for flags in the input line when `clink.colorize_input` is enabled.
<a name="color_hidden"></a>`color.hidden` | | Used when Clink displays file completions with the "hidden" attribute.
`color.horizscroll`          |         | Used when Clink displays the `<` or `>` horizontal scroll indicators when Readline's `horizontal-scroll-mode` variable is set.
`color.input`                |         | Used when Clink displays the input line text. Note that when `clink.colorize_input` is disabled, the entire input line is displayed using `color.input`.
`color.interact`             | `bold`  | Used when Clink displays text or prompts such as a pager's `--More?--` prompt.
`color.message`              | `default` | The color for the message area (e.g. the search prompt message, digit argument prompt message, etc).
`color.modmark`              |         | Used when Clink displays the `*` mark on modified history lines when Readline's `mark-modified-lines` variable and Clink's `color.input` setting are both set. Falls back to `color.input` if not set.
`color.prompt`               |         | When set, this is used as the default color for the prompt.  But it's overridden by any colors set by <a href="#customisingtheprompt">Customising The Prompt</a>.
<a name="color_readonly"></a>`color.readonly` | | Used when Clink displays file completions with the "readonly" attribute.
`color.selected_completion`  |         | The color for the selected completion with the clink-select-complete command.  If no color is set, then bright reverse video is used.
`color.selection`            |         | The color for selected text in the input line.  If no color is set, then reverse video is used.
`color.unexpected`           | `default` | The color for unexpected arguments in the input line when `clink.colorize_input` is enabled.
`debug.log_terminal`         | False   | Logs all terminal input and output to the clink.log file.  This is intended for diagnostic purposes only, and can make the log file grow significantly.
`doskey.enhanced`            | True    | Enhanced Doskey adds the expansion of macros that follow `\|` and `&` command separators and respects quotes around words when parsing `$1`..`$9` tags. Note that these features do not apply to Doskey use in Batch files.
`exec.cwd`                   | True    | When matching executables as the first word (`exec.enable`), include executables in the current directory. (This is implicit if the word being completed is a relative path).
`exec.dirs`                  | True    | When matching executables as the first word (`exec.enable`), also include directories relative to the current working directory as matches.
`exec.enable`                | True    | Match executables when completing the first word of a line.
`exec.path`                  | True    | When matching executables as the first word (`exec.enable`), include executables found in the directories specified in the `%PATH%` environment variable.
`exec.space_prefix`          | True    | If the line begins with whitespace then Clink bypasses executable matching (`exec.path`) and will do normal files matching instead.
`files.hidden`               | True    | Includes or excludes files with the "hidden" attribute set when generating file lists.
`files.system`               | False   | Includes or excludes files with the "system" attribute set when generating file lists.
`history.dont_add_to_history_cmds` | `exit history` | List of commands that aren't automatically added to the history. Commands are separated by spaces, commas, or semicolons. Default is `exit history`, to exclude both of those commands.
`history.dupe_mode`          | `erase_prev` | If a line is a duplicate of an existing history entry Clink will erase the duplicate when this is set to 'erase_prev'. Setting it to 'ignore' will not add duplicates to the history, and setting it to 'add' will always add lines (except when overridden by `history.sticky_search`).
`history.expand_mode`        | `not_quoted` | The `!` character in an entered line can be interpreted to introduce words from the history. This can be enabled and disable by setting this value to `on` or `off`. Values of `not_squoted`, `not_dquoted`, or `not_quoted` will skip any `!` character quoted in single, double, or both quotes respectively.
`history.ignore_space`       | True    | Ignore lines that begin with whitespace when adding lines in to the history.
`history.max_lines`          | 2500    | The number of history lines to save if `history.save` is enabled (1 to 50000).
`history.save`               | True    | Saves history between sessions. When disabled, history is neither read from nor written to a master history list; history for each session exists only in memory until the session ends.
`history.shared`             | False   | When history is shared, all instances of Clink update the master history list after each command and reload the master history list on each prompt.  When history is not shared, each instance updates the master history list on exit.
`history.sticky_search`      | False   | When enabled, reusing a history line does not add the reused line to the end of the history, and it leaves the history search position on the reused line so next/prev history can continue from there (e.g. replaying commands via <kbd>Up</kbd> several times then <kbd>Enter</kbd>, <kbd>Down</kbd>, <kbd>Enter</kbd>, etc).
`lua.break_on_error`         | False   | Breaks into Lua debugger on Lua errors.
`lua.break_on_traceback`     | False   | Breaks into Lua debugger on `traceback()`.
`lua.debug`                  | False   | Loads a simple embedded command line debugger when enabled. Breakpoints can be added by calling `pause()`.
`lua.path`                   |         | Value to append to `package.path`. Used to search for Lua scripts specified in `require()` statements.
<a name="lua_reload_scripts"></a>`lua.reload_scripts` | False | When false, Lua scripts are loaded once and are only reloaded if forced (see <a href="#lua-scripts-location">The Location of Lua Scripts</a> for details).  When true, Lua scripts are loaded each time the edit prompt is activated.
`lua.strict`                 | True    | When enabled, argument errors cause Lua scripts to fail.  This may expose bugs in some older scripts, causing them to fail where they used to succeed. In that case you can try turning this off, but please alert the script owner about the issue so they can fix the script.
`lua.traceback_on_error`     | False   | Prints stack trace on Lua errors.
`match.expand_envvars`       | False   | Expands environment variables in a word before performing completion.
`match.ignore_accent`        | True    | Controls accent sensitivity when completing matches. For example, `ä` and `a` are considered equivalent with this enabled.
`match.ignore_case`          | `relaxed` | Controls case sensitivity when completing matches. `off` = case sensitive, `on` = case insensitive, `relaxed` = case insensitive plus `-` and `_` are considered equal.
`match.sort_dirs`            | `with`  | How to sort matching directory names. `before` = before files, `with` = with files, `after` = after files.
`match.translate_slashes`    | `system` | File and directory completions can be translated to use consistent slashes.  The default is `system` to use the appropriate path separator for the OS host (backslashes on Windows).  Use `slash` to use forward slashes, or `backslash` to use backslashes.  Use `off` to turn off translating slashes from custom match generators.
`match.wild`                 | True    | Matches `?` and `*` wildcards when using any of the completion commands.  Turn this off to behave how bash does, and not match wildcards (but `glob-complete-word` always matches wildcards).
`prompt.async`               | True    | Enables asynchronous prompt refresh.  Turn this off if prompt filter refreshes are annoying or cause problems.
`readline.hide_stderr`       | False   | Suppresses stderr from the Readline library.  Enable this if Readline error messages are getting in the way.
`terminal.adjust_cursor_style`| True   | When enabled, Clink adjusts the cursor shape and visibility to show Insert Mode, produce the visible bell effect, avoid disorienting cursor flicker, and to support ANSI escape codes that adjust the cursor shape and visibility. But it interferes with the Windows 10 Cursor Shape console setting. You can make the Cursor Shape setting work by disabling this Clink setting (and the features this provides).
`terminal.differentiate_keys`| False   | When enabled, pressing <kbd>Ctrl</kbd> + <kbd>H</kbd> or <kbd>I</kbd> or <kbd>M</kbd> or <kbd>[</kbd> generate special key sequences to enable binding them separately from <kbd>Backspace</kbd> or <kbd>Tab</kbd> or <kbd>Enter</kbd> or <kbd>Esc</kbd>.
`terminal.emulation`         | `auto`  | Clink can either emulate a virtual terminal and handle ANSI escape codes itself, or let the console host natively handle ANSI escape codes. `native` = pass output directly to the console host process, `emulate` = clink handles ANSI escape codes itself, `auto` = emulate except when running in ConEmu, Windows Terminal, or Windows 10 new console.
`terminal.raw_esc`           | False | When enabled, pressing Esc sends a literal escape character like in Unix/etc terminals.  This setting is disabled by default to provide a more predictable, reliable, and configurable input experience on Windows.  Changing this only affects future Clink sessions, not the current session.
`terminal.use_altgr_substitute`| False | Support Windows' <kbd>Ctrl</kbd>-<kbd>Alt</kbd> substitute for <kbd>AltGr</kbd>. Turning this off may resolve collisions with Readline's key bindings.

<p/>

> **Compatibility Notes:**
> - The `esc_clears_line` setting has been replaced by a `clink-reset-line` command that is by default bound to the <kbd>Escape</kbd> key.  See [Key Bindings](#keybindings) and [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html) for more information.
> - The `match_colour` setting has been removed, and Clink now supports Readline's completion coloring.  See [Completion Colors](#completioncolors) for more information.

<a name="colorsettings"></a>

## Color Settings

### Friendly Color Names

The Clink color settings use the following syntax:

<code>[<span class="arg">attributes</span>] [<span class="arg">foreground_color</span>] [on [<span class="arg">background_color</span>]]</code>

Optional attributes (can be abbreviated to 3 letters):
- `bold` or `nobold` adds or removes boldface (usually represented by forcing the color to use high intensity if it doesn't already).
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

<a name="filelocations"></a>

## File Locations

Settings and history are persisted to disk from session to session. The location of these files depends on which distribution of Clink was used. If you installed Clink using the .exe installer then Clink uses the current user's non-roaming application data directory. This user directory is usually found in one of the following locations;

- Windows XP: `c:\Documents and Settings\<username>\Local Settings\Application Data\clink`
- Windows Vista onwards: `c:\Users\<username>\AppData\Local\clink`

All of the above locations can be overridden using the <code>--profile <span class="arg">path</span></code> command line option which is specified when injecting Clink into cmd.exe using `clink inject`.  Or with the `%CLINK_PROFILE%` environment variable if it is already present when Clink is injected (this envvar takes precedence over any other mechanism of specifying a profile directory, if more than one was used).

You can use `clink info` to find the directories and configuration files for the current Clink session.

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
When Clink is installed for autorun, the automatic inject can be overridden by setting the <code>CLINK_NOAUTORUN</code> environment variable (to any value).<br/>
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
<dt>clink installscripts</dt>
<dd>
Adds a path to search for Lua scripts.<br/>
The path is stored in the registry and applies to all installations of Clink, regardless where their config paths are, etc.  This is intended to make it easy for package managers like Scoop to be able to install (and uninstall) scripts for use with Clink.</br>
See <code>clink installscripts --help</code> for more information.</dd>
</p>

<p>
<dt>clink uninstallscripts</dt>
<dd>
Removes a path added by `clink installscripts`.</br>
See <code>clink uninstallscripts --help</code> for more information.</dd>
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
Echos key sequences to use in the .inputrc files for binding keys to Clink commands.  Each key pressed prints the associated key sequence on a separate line, until <kbd>Ctrl</kbd>+<kbd>C</kbd> is pressed.</dd>
</p>

## Portable Configuration

Sometimes it's useful to run Clink from a flash drive or from a network share, especially if you want to use Clink on someone else's computer.

Here's how you can set up a portable configuration for Clink:

1. Put your Lua scripts and other tools in the same directory as the Clink executable files.  For example fzf.exe, z.cmd, oh-my-posh.exe, or etc can all go in the same directory on a flash drive or network share.
2. Make a batch file such as `portable.bat` that injects Clink using a specific profile directory.
   - On a flash drive, you can have a portable profile in a subdirectory under the Clink directory.
   - On a network share, you'll want to copy some initial settings into a local profile directory (a profile directory on a network share will be slow).
3. In any cmd.exe window on any computer, you can then run the `portable.bat` script to inject Clink and have all your favorite settings and key bindings work.

Here are some sample scripts:

### portable.bat (on a flash drive)

This sample script assumes the portable.bat script is in the Clink directory, and it uses a `clink_portable` profile directory under the Clink directory.

```cmd
@echo off
rem -- Do any other desired configuration here, such as loading a doskey macro file.
call "%~dp0clink.bat" inject --profile "%~dp0clink_portable" %1 %2 %3 %4 %5 %6 %7 %8 %9
```

### portable.bat (on a network share)

This sample script assumes the portable.bat script is in the Clink directory, and that there is a file `portable_clink_settings` with the settings you want to copy to the local profile directory.

```cmd
@echo off
if not exist "%TEMP%\clink_portable" md "%TEMP%\clink_portable" >nul
if not exist "%TEMP%\clink_portable\clink_settings" copy "%~dp0portable_clink_settings" "%TEMP%\clink_portable\clink_settings" >nul
rem -- Do any other desired configuration here, such as loading a doskey macro file.
call "%~dp0clink.bat" inject --profile "%TEMP%\clink_portable" %1 %2 %3 %4 %5 %6 %7 %8 %9
```

<a name="configreadline"></a>

# Configuring Readline

Readline itself can also be configured to add custom keybindings and macros by creating a Readline init file. There is excellent documentation for all the options and commands available to configure Readline in Readline's [manual](https://tiswww.cwru.edu/php/chet/readline/rltop.html#Documentation).

Clink searches in the directories referenced by the following environment variables and loads the first `.inputrc` or `_inputrc` file found, in the order listed here:
- `%CLINK_INPUTRC%`
- The Clink profile directory (see the "state" line from `clink info`).
- `%USERPROFILE%`
- `%LOCALAPPDATA%`
- `%APPDATA%`
- `%HOME%`

Other software that also uses Readline will also look for the `.inputrc` file (and possibly the `_inputrc` file too). To set macros and keybindings intended only for Clink one can use the Readline init file conditional construct like this; `$if clink [...] $endif`.

You can use `clink info` to find the directories and configuration file for the current Clink session.

> **Compatibility Notes:**
> - The `clink_inputrc_base` file from v0.4.8 is no longer used.
> - For backward compatibility, `clink_inputrc` is also loaded from the above locations, but it has been deprecated and may be removed in the future.
> - Clink v1.0.0a0 through Clink v1.2.27 accidentally loaded up to one Readline init file from each of the searched directories. That was incorrect behavior for loading Readline init files and has been fixed. If similar behavior is still desired, consider using the `$include` directive in the Readline init file, to load additional files.

### Basic Format

The quick version is that mostly you'll use these kinds of lines:
- <code><span class="arg">keyname</span>: <span class="arg">command</span></code>
- <code><span class="arg">keyname</span>: "<span class="arg">literal text</span>"</code>
- <code><span class="arg">keyname</span>: "luafunc:<span class="arg">lua_function_name</span>"</code>
- <code>set <span class="arg">varname</span> <span class="arg">value</span></code>

If a Readline macro begins with "luafunc:" then Clink interprets it as a Lua key binding and will invoke the named Lua function.  Function names can include periods (such as `foo.bar`) but cannot include any other punctuation.  See <a href="#luakeybindings">Lua Key Bindings</a> for more information.

Refer to the Readline [manual](https://tiswww.cwru.edu/php/chet/readline/rltop.html#Documentation) for a more thorough explanation of the .inputrc file format, list of available commands, and list of configuration variables and their values.

See [Key Bindings](#keybindings) for more information about binding keys in Clink.

### New Configuration Variables

Clink adds some new configuration variables to Readline, beyond what's described in the Readline manual:

Name | Default | Description
:-:|:-:|---
`completion-auto-query-items`|on|Automatically prompts before displaying completions if they need more than half a screen page.
`history-point-at-end-of-anchored-search`|off|Puts the cursor at the end of the line when using `history-search-forward` or `history-search-backward`.
`menu-complete-wraparound`|on|The `menu-complete` family of commands wraps around when reaching the end of the possible completions.
`search-ignore-case`|on|Controls whether the history search commands ignore case.

### New Commands

Clink adds some new commands to Readline, beyond what's described in the Readline manual:

Name | Description
:-:|---
`add-history`|Adds the current line to the history without executing it, and clears the editing line.
`alias-expand-line`|A synonym for `clink-expand-doskey-alias`.
`clink-complete-numbers`|Like `complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-copy-cwd`|Copy the current working directory to the clipboard.
`clink-copy-line`|Copy the current line to the clipboard.
`clink-copy-word`|Copy the word at the cursor to the clipboard, or copies the nth word if a numeric argument is provided via the `digit-argument` keys.
`clink-ctrl-c`|Discards the current line and starts a new one (like <kbd>Ctrl</kbd>+<kbd>C</kbd> in CMD.EXE).
`clink-diagnostics`|Show internal diagnostic information.
`clink-exit`|Replaces the current line with `exit` and executes it (exits the shell instance).
`clink-expand-doskey-alias`|Expand the doskey alias (if any) at the beginning of the line.
`clink-expand-env-var`|Expand the environment variable (e.g. `%FOOBAR%`) at the cursor.
`clink-expand-history`|Perform history expansion in the current input line.  See the [History](https://tiswww.cwru.edu/php/chet/readline/history.html) manual for information on history expansion.
`clink-expand-history-and-alias`|Perform history and doskey alias expansion in the current input line.  See the [History](https://tiswww.cwru.edu/php/chet/readline/history.html) manual for information on history expansion.
`clink-expand-line`|Perform history, doskey alias, and environment variable expansion in the current input line.  See the [History](https://tiswww.cwru.edu/php/chet/readline/history.html) manual for information on history expansion.
`clink-find-conhost`|Activates the "Find" dialog when running in a standard console window (hosted by the OS conhost).  This is equivalent to picking "Find..." from the console window's system menu.
`clink-insert-dot-dot`|Inserts `..\` at the cursor.
`clink-mark-conhost`|Activates the "Mark" mode when running in a standard console window (hosted by the OS conhost).  This is equivalent to picking "Mark" from the console window's system menu.
`clink-menu-complete-numbers`|Like `menu-complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-menu-complete-numbers-backward`|Like `menu-complete-backward`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-old-menu-complete-numbers`|Like `old-menu-complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-old-menu-complete-numbers-backward`|Like `old-menu-complete-backward`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-paste`|Paste the clipboard at the cursor.
`clink-popup-complete`|Show a [popup window](#popupwindow) that lists the available completions.
`clink-popup-complete-numbers`|Like `clink-popup-complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-popup-directories`|Show a [popup window](#popupwindow) of recent current working directories.  In the popup, use <kbd>Enter</kbd> to `cd /d` to the highlighted directory.
`clink-popup-history`|Show a [popup window](#popupwindow) that lists the command history (if any text precedes the cursor then it uses an anchored search to filter the list).  In the popup, use <kbd>Enter</kbd> to execute the highlighted command.
`clink-popup-show-help`|Show a [popup window](#popupwindow) that lists the currently active key bindings, and can invoke a selected key binding.  The default key binding for this is <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>H</kbd>.
`clink-reload`|Reloads the .inputrc file and the Lua scripts.
`clink-reset-line`|Clears the current line.
`clink-scroll-bottom`|Scroll the console window to the bottom (the current input line).
`clink-scroll-line-down`|Scroll the console window down one line.
`clink-scroll-line-up`|Scroll the console window up one line.
`clink-scroll-page-down`|Scroll the console window down one page.
`clink-scroll-page-up`|Scroll the console window up one page.
`clink-scroll-top`|Scroll the console window to the top.
`clink-select-complete`|Like `complete`, but shows interactive menu of matches and responds to arrow keys and typing to filter the matches.
`clink-show-help`|Lists the currently active key bindings using friendly key names.  A numeric argument affects showing categories and descriptions:  0 for neither, 1 for categories, 2 for descriptions, 3 for categories and descriptions (the default).
`clink-show-help-raw`|Lists the currently active key bindings using raw key sequences.  A numeric argument affects showing categories and descriptions:  0 for neither, 1 for categories, 2 for descriptions, 3 for categories and descriptions (the default).
`clink-up-directory`|Changes to the parent directory.
`clink-what-is`|Show the key binding for the next key sequence that is input.
`cua-backward-char`|Extends the selection and moves back a character.
`cua-backward-word`|Extends the selection and moves back a word.
`cua-beg-of-line`|Extends the selection and moves to the start of the current line.
`cua-copy`|Copies the selection to the clipboard.
`cua-cut`|Cuts the selection to the clipboard.
`cua-end-of-line`|Extends the selection and moves to the end of the line.
`cua-forward-char`|Extends the selection and moves forward a character.
`cua-forward-word`|Extends the selection and moves forward a word.
`cua-select-all`|Extends the selection to the entire current line.
`edit-and-execute-command`|Invoke an editor on the current input line, and execute the result as commands.  This attempts to invoke %VISUAL%, %EDITOR%, or notepad.exe as the editor, in that order.
`history-and-alias-expand-line`|A synonym for `clink-expand-history-and-alias`.
`history-expand-line`|A synonym for `clink-expand-history`.
`insert-last-argument`|A synonym for `yank-last-arg`.
`old-menu-complete-backward`|Like `old-menu-complete`, but in reverse.
`remove-history`|While searching history, removes the current line from the history.
`shell-expand-line`|A synonym for `clink-expand-line`.

<a name="completioncolors"></a>

## Completion Colors

The `%LS_COLORS%` environment variable provides color definitions as a series of color definitions separated by colons (`:`).  Each definition is a either a two character type id or a file extension, followed by an equals sign and then the [SGR parameters](https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_parameters) for an ANSI escape code.  The two character type ids are listed below.

When the `colored-completion-prefix` [Readline setting](#configreadline) is configured to `on`, then the "so" color from `%LS_COLORS%` is used to color the common prefix when displaying possible completions.  The default for "so" is magenta, but for example `set LS_COLORS=so=90` sets the color to bright black (which shows up as a dark gray).

When `colored-stats` is configured to `on`, then the color definitions from `%LS_COLORS%` are used to color file completions according to their file type or extension.    Multiple definitions are separated by colons.  Also, since `%LS_COLORS%` doesn't cover readonly files, hidden files, doskey aliases, or shell commands the [color.readonly](#color_readonly), [color.hidden](#color_hidden), [color.doskey](#color_doskey), and [color.cmd](#color_cmd) Clink settings exist to cover those.

Type|Description|Default
-|-|-
`di`|Directories.|`01;34` (bright blue)
`ex`|Executable files.|`01;32` (bright green)
`fi`|Normal files.|
`ln`|Symlinks.  When `ln=target` then symlinks are colored according to the target of the symlink.|`target`
`mi`|Missing file or directory.|
`no`|Normal color.  This is used for anything not covered by one of the other types.<br/>It may be overridden by various other Clink color settings as appropriate depending on the completion type.|
`or`|Orphaned symlink (the target of the symlink is missing).|
`so`|Common prefix for possible completions.|`01;35` (bright magenta)

Here is an example where `%LS_COLORS%` defines colors for various types.

```plaintext
set LS_COLORS=so=90:fi=97:di=93:ex=92:*.pdf=30;105:*.md=4
```

- `so=90` uses bright black (dark gray) for the common prefix for possible completions.
- `fi=97` uses bright white for files.
- `di=93` uses bright yellow for directories.
- `ex=92` uses bright green for executable files.
- `*.pdf=30;105` uses black on bright magenta for .pdf files.
- `*.md=4` uses underline for .md files.

<a name="popupwindow"></a>

## Popup window

The `clink-popup-complete`, `clink-popup-directories`, and `clink-popup-history` [Readline commands](#configreadline) show a searchable popup window that lists the available completions, directory history, or command history.  Here's how the popup windows work:

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

<a name="extending-clink"></a>

# Extending Clink With Lua

Clink can be extended with <a href="https://www.lua.org/docs.html">Lua</a> scripts to customize startup actions, create completion matches, customize the prompt, and more.  The following sections describe these in more detail and show some examples.

<a name="lua-scripts-location"></a>

## The Location of Lua Scripts

Clink loads all Lua scripts it finds in these directories:
1. All directories listed in the `clink.path` setting, separated by semicolons.
2. If `clink.path` is not set, then the DLL directory and the profile directory are used (see <a href="#filelocations">File Locations</a> for info about the profile directory).
3. All directories listed in the `%CLINK_PATH%` environment variable, separated by semicolons.
4. All directories registered by the `clink installscripts` command.

Lua scripts are loaded once and are only reloaded if forced because the scripts locations change, the `clink-reload` command is invoked (<kbd>Ctrl</kbd>+<kbd>X</kbd>,<kbd>Ctrl</kbd>+<kbd>R</kbd>), or the `lua.reload_scripts` setting changes (or is True).

Run `clink info` to see the script paths for the current session.

### Tips for starting to write Lua scripts

- Loading a Lua script executes it; so when Clink loads Lua scripts from the locations above, it executes the scripts.
- Code not inside a function is executed immediately when the script is loaded.
- Usually scripts will register functions to customize various behaviors:
  - Generate completion matches.
  - Apply color to input text.
  - Customize the prompt.
  - Perform actions before or after the user gets to edit each input line.
  - Provide new custom commands that can be bound to keys via the <a href="#luakeybindings">luafunc: key macro syntax</a>.
- Often scripts will also define some functions and variables for use by itself and/or other scripts.

The following sections describe these in more detail and show some examples.

<a name="matchgenerators"></a>

## Match Generators

These are Lua functions that are called as part of Readline's completion process (for example when pressing <kbd>Tab</kbd>).

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

Here is an example script that supplies git branch names as matches for `git checkout`.  This example doesn't handle git aliases, but that could be added with additional script code.

```lua
#INCLUDE [examples\ex_generate.lua]
```

### The :getwordbreakinfo() Function

If needed, a generator can optionally influence word breaking for the end word by defining a `:getwordbreakinfo()` function.

The function takes a <span class="arg">line_state</span> <a href="#line">line</a> object that has information about the current line.  If it returns nil or 0, the end word is truncated to 0 length.  This is the normal behavior, which allows Clink to collect and cache all matches and then filter them based on typing.  Or it can return two numbers:  word break length and an optional end word length.  The end word is split at the word break length:  one word contains the first word break length characters from the end word (if 0 length then it's discarded), and the next word contains the rest of the end word truncated to the optional word length (0 if omitted).

A good example to look at is Clink's own built-in environment variable match generator.  It has a `:getwordbreakinfo()` function that understands the `%` syntax of environment variables and produces word break info accordingly.

When the environment variable match generator's `:getwordbreakinfo()` function sees the end word is `abc%USER` it returns `3,1` so that the last two words become "abc" and "%" so that its generator knows it can do environment variable matching.  But when it sees `abc%FOO%def` it returns `8,0` so that the last two words become "abc%FOO%" and "" so that its generator won't do environment variable matching, and also so other generators can produce matches for what follows, since "%FOO%" is an already-completed environment variable and therefore should behave like a word break.  In other words, it breaks the end word differently depending on whether the number of percent signs is odd or even, to account for environent variable syntax rules.

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

### More Advanced Stuff

<a name="filteringmatchcompletions"></a>

#### Filtering Match Completions

A match generator or <a href="#luakeybindings">luafunc: key binding</a> can use <a href="#clink.onfiltermatches">clink.onfiltermatches()</a> to register a function that will be called after matches are generated but before they are displayed or inserted.

The function receives a table argument containing the matches to be displayed, a string argument indicating the completion type, and a boolean argument indicating whether filename completion is desired. The table argument has a `match` string field and a `type` string field; these are the same as in <a href="builder:addmatch">builder:addmatch()</a>.

The possible completion types are:

Type | Description | Example
---|---|---
`"?"`  | List the possible completions. | `possible-completions` or `popup-complete`
`"*"`  |Insert all of the possible completions. | `insert-completions`
`"\t"` | Do standard completion. | `complete`
`"!"`  | Do standard completion, and list all possible completions if there is more than one. | `complete` (when the `show-all-if-ambiguous` config variable is set)
`"@"`  | Do standard completion, and list all possible completions if there is more than one and partial completion is not possible. | `complete` (when the `show-all-if-unmodified` config variable is set)
`"%"`  | Do menu completion (cycle through possible completions). | `menu-complete` or `old-menu-complete`

The return value is a table with the input matches filtered as desired. The match filter function can remove matches, but cannot add matches (use a match generator instead).  If only one match remains after filtering, then many commands will insert the match without displaying it.  This makes it possible to spawn a process (such as <a href="https://github.com/junegunn/fzf">fzf</a>) to perform enhanced completion by interactively filtering the matches and keeping only one selected match.

```lua
#INCLUDE [examples\ex_fzf.lua]
```

<a name="filteringthematchdisplay"></a>

#### Filtering The Match Display

In some instances it may be preferable to display potential matches in an alternative form than the generated matches passed to and used internally by Readline. For example, it might be desirable to display a `*` next to some matches, or to show additional information about each match.

A match generator can use <a href="#clink.ondisplaymatches">clink.ondisplaymatches()</a> to register a function that will be called before matches are displayed (this is reset every time match generation is invoked).

The function receives a table argument containing the matches to be displayed, and a boolean argument indicating whether they'll be displayed in a popup window. The table argument has a `match` string field and a `type` string field; these are the same as in <a href="builder:addmatch">builder:addmatch()</a>. The return value is a table with the input matches filtered as required by the match generator.

The returned table can also optionally include a `display` string field and a `description` string field. When present, `display` will be displayed instead of the `match` field, and `description` will be displayed next to the match. Putting the description in a separate field enables Clink to align the descriptions in a column.

Filtering the match display can affect completing matches: the `match` field is what gets inserted.  It can also affect displaying matches: the `display` field is displayed if present, otherwise the `match` field is displayed.

If a match's `type` is "none" or its `match` field is different from its `display` field then the match is displayed using the color specified by the [color.filtered](#color_filtered) Clink setting, otherwise normal completion coloring is applied.  The `display` and `description` fields can include ANSI escape codes to apply other colors if desired.

```lua
local function my_filter(matches, popup)
    local new_matches = {}
    local magenta = "\x1b[35m"
    local filtered = settings.get("color.filtered")
    for _,m in ipairs(matches) do
        if m.match:find("[0-9]") then
            -- Ignore matches with one or more digits.
        else
            -- Keep the match, and also add a magenta * prefix to directory matches.
            if m.type:find("^dir") then
                m.display = magenta.."*"..filtered..m.match
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

<a name="argumentcompletion"></a>

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

If a command doesn't have an argmatcher but is a doskey macro, Clink automatically expands the doskey macro and looks for an argmatcher for the expanded command.  A macro like `gco=git checkout $*` automatically reuses a `git` argmatcher and produces completions for its `checkout` argument.  However, it only expands the doskey macro up to the first `$`, so complex aliases like `foo=app 2$gnul text $*` or `foo=$2 $1` might behave strangely.

### More Advanced Stuff

#### Linking Parsers

There are often situations where the parsing of a command's arguments is dependent on the previous words (`git merge ...` compared to `git log ...` for example). For these scenarios Clink allows you to link parsers to arguments' words using Lua's concatenation operator.

```lua
a_parser = clink.argmatcher():addarg({ "foo", "bar" })
b_parser = clink.argmatcher():addarg({ "abc", "123" })
c_parser = clink.argmatcher()
c_parser:addarg({ "foobar" .. a_parser })   -- Arg #1 is "foobar", which has args "foo" or "bar".
c_parser:addarg({ b_parser })               -- Arg #2 is "abc" or "123".
```

As the example above shows, it is also possible to use a parser without concatenating it to a word.

When Clink follows a link to a parser it will only return to the previous parser when the linked parser runs out of arguments.

#### Flags With Arguments

Parsers can be concatenated with flags, too.

Here's an example of a flag that takes an argument:

```lua
clink.argmatcher("git")
:addarg({
    "merge"..clink.argmatcher():addflags({
        "--strategy"..clink.argmatcher():addarg({
            "resolve",
            "recursive",
            "ours",
            "octopus",
            "subtree",
        })
    })
})
```

A `:` or `=` at the end of a flag indicates the flag takes an argument but requires no space between the flag and its argument.  If such a flag is not linked to a parser, then it automatically gets linked to a parser to match files.  Here's an example with a few flags that take arguments without a space in between:

```lua
#INCLUDE [examples\ex_findstr.lua]
```

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

<a name="classifywords"></a>

## Coloring The Input Text

When the `clink.colorize_input` [setting](#clinksettings) is enabled, [argmatcher](#argumentcompletion) automatically apply colors to the input text by parsing it.

It's possible for an argmatcher to provide a function to override how its arguments are colored.  This function is called once for each of the argmatcher's arguments.

It's also possible to register a classifier function for the whole input line.  This function is very similar to a match generator; classifier functions are called in priority order, and a classifier can choose to stop the classification process.

### More Advanced Stuff

#### Setting a classifier function in an argmatcher

In cases where an [argmatcher](#argumentcompletion) isn't able to color the input text in the desired manner, it's possible to supply a classifier function that overrides how the argmatcher colors the input text.  An argmatcher's classifier function is called once for each word the argmatcher parses, but it can classify any words (not just the word it was called for).  Each argmatcher can have its own classifier function, so when there are linked argmatchers more than one function may be invoked.

Words are colored by classifying the words, and each classification has an associated color.  See [word_classifications:classifyword()](#word_classifications:classifyword) for the available classification codes.

The `clink set` command has different syntax depending on the setting type, so the argmatcher for `clink` needs help in order to get everything right.  A custom generator function parses the input text to provide appropriate matches, and a custom classifier function applies appropriate coloring.

```lua
#INCLUDE [examples\ex_classify_samp.lua]
```

#### Setting a classifier function for the whole input line

In some cases it may be desireable to use a custom classifier to apply coloring in an input line.

First create a classifier object:

```lua
local my_classifier = clink.classifier(priority)
```

The <span class="arg">priority</span> argument is a number that influences when the classifier gets called, with lower numbers going before higher numbers.

Next define a classify function on the object, taking the following form:

```lua
function my_classifier:classify(commands)
    -- commands is a table of { line_state, classifications } objects, one per
    -- command in the input line.  For example, "echo hello & echo world" is two
    -- commands:  "echo hello" and "echo world".
end
```

<span class="arg">commands</span> is a table of tables, with the following scheme:
<span class="tablescheme">{ {line_state:<a href="#line">line</a>, classifications:<a href="#word_classifications">word_classifications</a>}, ... }</span>.

If no further classifiers need to be called then the function should return true.  Returning false or nil continues letting other classifiers get called.

```lua
#INCLUDE [examples\ex_cmd_sep.lua]
```

<a name="customisingtheprompt"></a>

## Customising The Prompt

Before Clink displays the prompt it filters the prompt through [Lua](#extending-clink) so that the prompt can be customised. This happens each and every time that the prompt is shown which allows for context sensitive customisations (such as showing the current branch of a git repository).

Writing a prompt filter is straightforward:
1. Create a new prompt filter by calling `clink.promptfilter()` along with a priority id which dictates the order in which filters are called. Lower priority ids are called first.
2. Define a `:filter()` function on the returned prompt filter.

The filter function takes a string argument that contains the filtered prompt so far.  If the filter function returns nil, it has no effect.  If the filter function returns a string, that string is used as the new filtered prompt (and may be further modified by other prompt filters with higher priority ids).  If the filter function returns a string and a boolean, then if the boolean is false the prompt filtering is done and no further filter functions are called.

```lua
local p = clink.promptfilter(30)
function p:filter(prompt)
    return "new prefix "..prompt.." new suffix" -- Add ,false to stop filtering.
end
```

The following example illustrates setting the prompt, modifying the prompt, using ANSI escape code for colors, running a git command to find the current branch, and stopping any further processing.

```lua
#INCLUDE [examples\ex_prompt.lua]
```

The resulting prompt will look like this:

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black"><span style="color:#00ff00">Wed 12:54</span> <span style="color:#ffff00">c:\dir</span> <span style="color:#008080">[master]</span>
<span style="color:#cccccc">&gt;&nbsp;_</span>
</code></pre>

...except on Wednesdays, when it will look like this:

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black"><span style="color:#00ff00">Wed 12:54</span> <span style="color:#ffff00">c:\dir</span> <span style="color:#cccccc">HAPPY HUMP DAY!&nbsp;_</span>
</code></pre>

<p/>

<a name="escapecodes"></a>

### ANSI escape codes in the prompt string

Readline needs to be told which characters in the prompt are unprintable or invisible.  To help with that, Clink automatically detects most standard ANSI escape codes (and most of ConEmu's non-standard escape codes) and the BEL character (^G, audible bell) and surrounds them with `\001` (^A) and `\002` (^B) characters.  For any other unprintable characters, the `\001` and `\002` characters need to be added manually.  Otherwise Readline misinterprets the length of the prompt and can display the prompt and input line incorrectly in some cases (especially if the input line wraps onto a second line).

### More Advanced Stuff

<a name="rightprompt"></a>

#### Right Side Prompt

In addition to the normal prompt filtering, Clink can also show a prompt on the right side of the first line of input.  The right side prompt defaults to the value of the `%CLINK_RPROMPT%` environment variable, if set, otherwise it is blank.  This right side prompt is automatically hidden if the input line text reaches it.

Clink expands CMD prompt `$` codes in `%CLINK_RPROMPT%`, with a few exceptions:  `$+` is not supported, `$_` ends the prompt string (it can't be more than one line), and `$V` is not supported.  Additionally, if `%CLINK_RPROMPT%` ends with `$M` then trailing spaces are trimmed from the expanded string, to maintain right alignment since `$M` includes a space if the current drive is a network drive (so e.g. `$t $d $m` is right-aligned regardless whether the current drive has a remote name).

The right side prompt can be filtered through [Lua](#extending-clink) just like the normal prompt can be.  Simply define a `:rightfilter()` function on the prompt filter returned by a call to `clink.promptfilter()`.  A prompt filter can define both `:filter()` and `:rightfilter()`, or can define only `:filter()`.

The `:rightfilter()` function works the same as the `:filter()` function, except that it operates on the right side prompt.  It takes a string argument that contains the filtered right side prompt so far.  If the rightfilter function returns nil, it has no effect.  If the rightfilter function returns a string, that string is used as the new filtered right side prompt (and may be further modified by other prompt filters with higher priority ids).  If either the rightfilter function or the normal filter function returns a string and a boolean, then if the boolean is false the prompt filtering is done and no further filter functions are called.

This example modifies the right side prompt by prepending the current date:

```lua
#INCLUDE [examples\ex_right_prompt.lua]
```

<br/>

> **Note:** If the console font and encoding are mismatched, or if some kinds of emoji are present, then the right side prompt might show up positioned incorrectly.  If that happens, try adjusting the font or encoding (e.g. sometimes running `chcp utf-8` can resolve positioning issues).

<a name="asyncpromptfiltering"></a>

#### Asynchronous Prompt Filtering

Prompt filtering needs to be fast, or it can interfere with using the shell (e.g. `git status` can be slow in a large repo).

Clink provides a way for prompt filters to do some initial work and set the prompt, continue doing work in the background, and then refresh the prompt again when the background work is finished.  This is accomplished by using [Lua coroutines](https://www.lua.org/manual/5.2/manual.html#2.6), but Clink simplifies and streamlines the process.

A prompt filter can call <a href="#clink.promptcoroutine">clink.promptcoroutine(my_func)</a> to run `my_func()` inside a coroutine.  Clink will automatically resume the coroutine repeatedly while input line editing is idle.  When `my_func()` completes, Clink will automatically refresh the prompt by triggering prompt filtering again.

Typically the motivation to use asynchronous prompt filtering is that one or more <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>(<span class="hljs-string">"some slow command"</span>)</code> calls take too long.  They can be replaced with <a href="#io.popenyield">io.popenyield()</a> calls inside the prompt coroutine to let them run in the background.

> **Global data:** If `my_func()` needs to use any global data, then it's important to use <a href="#clink.onbeginedit">clink.onbeginedit()</a> to register an event handler that can reset the global data for each new input line session.  Otherwise the data may accidentally "bleed" across different input line sessions.
>
> **Backward compatibility:** A prompt filter must handle backward compatibility itself if it needs to run on versions of Clink that don't support asynchronous prompt filtering (v1.2.9 and lower).  E.g. you can use <code><span class="hljs-keyword">if</span> clink.promptcoroutine <span class="hljs-keyword">then</span></code> to test whether the API exists.

The following example illustrates running `git status` in the background.  It also remembers the status from the previous input line, so that it can reduce flicker by using the color from last time until the background status operation completes.

```lua
#INCLUDE [examples\ex_async_prompt.lua]
```

# Miscellaneous

<a name="keybindings"></a>

## Key bindings

Key bindings are defined in .inputrc files.  See the [Configuring Readline](#configreadline) section for more information.

Here is the quick version:

- A key binding is <code><span class="arg">name</span>: <span class="arg">function</span></code> or <code><span class="arg">name</span>: "<span class="arg">literal text</span>"</code>.
- Key names are like this:
  - `C-a` and `"\C-a"` are both <kbd>Ctrl</kbd>+<kbd>a</kbd>.
  - `M-a` and `"\M-a"` are both <kbd>Alt</kbd>+<kbd>a</kbd>.
  - `M-C-a` and `"\M-\C-a"` are both <kbd>Alt</kbd>+<kbd>Ctrl</kbd>+<kbd>a</kbd>.
  - `hello` is just <kbd>h</kbd>; the `ello` is a syntax error and is silently discarded by Readline.
  - `"hello"` is the series of keys <kbd>h</kbd>,<kbd>e</kbd>,<kbd>l</kbd>,<kbd>l</kbd>,<kbd>o</kbd>.
  - Special keys like <kbd>Up</kbd> are represented by VT220 escape codes such as`"\e[A"` (see [Binding Special Keys](#specialkeys) for more info).
- Key bindings can be either functions or macros (literal text):
  - `blah-blah` binds to a function named "blah-blah".
  - `"blah-blah"` inserts the literal text "blah-blah".

You can use `clink info` to find the directories and configuration files for the current Clink session.

Here is an example `.inputrc` file with key bindings that I use myself:

<pre><code class="plaintext"><span class="hljs-meta">$if clink</span>           <span class="hljs-comment"># begin clink-only section</span>

<span class="hljs-comment"># The following key bindings are for emacs mode.</span>
<span class="hljs-meta">set keymap emacs</span>

<span class="hljs-comment"># Completion key bindings.</span>
<span class="hljs-string">"\t"</span>:               old-menu-complete               <span class="hljs-comment"># Tab</span>
<span class="hljs-string">"\e[Z"</span>:             old-menu-complete-backward      <span class="hljs-comment"># Shift+Tab</span>
<span class="hljs-string">"\e[27;5;9~"</span>:       clink-popup-complete            <span class="hljs-comment"># Ctrl+Tab</span>
<span class="hljs-string">"\x1b[27;5;32~"</span>:    clink-select-complete           <span class="hljs-comment"># Ctrl+Space</span>

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

<span class="hljs-comment"># Some key bindings handy in default (conhost) console windows.</span>
M-b:                                                <span class="hljs-comment"># Alt+B (cleared because I redefined Alt+F)</span>
M-f:                clink-find-conhost              <span class="hljs-comment"># Alt+F for "Find..." from the console's system menu</span>
M-m:                clink-mark-conhost              <span class="hljs-comment"># Alt+M for "Mark" from the console's system menu</span>

<span class="hljs-comment"># Some key bindings for interrogating the Readline configuration.</span>
<span class="hljs-string">"\C-x\C-f"</span>:         dump-functions                  <span class="hljs-comment"># Ctrl+X, Ctrl+F</span>
<span class="hljs-string">"\C-x\C-m"</span>:         dump-macros                     <span class="hljs-comment"># Ctrl+X, Ctrl+M</span>
<span class="hljs-string">"\C-x\C-v"</span>:         dump-variables                  <span class="hljs-comment"># Ctrl+X, Ctrl+V</span>

<span class="hljs-comment"># Misc other key bindings.</span>
<span class="hljs-string">"\e[5;6~"</span>:          clink-popup-directories         <span class="hljs-comment"># Ctrl+Shift+PgUp</span>
C-_:                kill-line                       <span class="hljs-comment"># Ctrl+- (replaces `undo`)</span>

<span class="hljs-meta">$endif</span>              <span class="hljs-comment"># end clink-only section</span>
</code></pre>

The `clink-show-help` command is bound to <kbd>Alt</kbd>+<kbd>H</kbd> and lists all currently active key bindings.  The list displays "friendly" key names, and these names are generally not suitable for use in .inputrc files.  For example "Up" is the friendly name for `"\e[A"`, and "A-C-F2" is the friendly name for `"\e\e[1;5Q"`.  To see key sequence strings suitable for use in .inputrc files use `clink echo` or <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd>.

> **Note:** Third party console hosts such as ConEmu may have their own key bindings that supersede Clink.  They usually have documentation for how to change or disable their key bindings to allow console programs to handle the keys instead.

### Discovering Clink key sequences

Clink provides an easy way to find the key sequence for any key combination that Clink supports. Run `clink echo` and then press key combinations; the associated key binding sequence is printed to the console output and can be used for a key binding in the inputrc file.

A chord can be formed by concatenating multiple key binding sequences. For example, `"\C-X"` and `"\e[H"` can be concatenated to form `"\C-X\e[H"` representing the chord <kbd>Ctrl</kbd>+<kbd>X</kbd>,<kbd>Home</kbd>.

<a name="specialkeys"></a>

### Binding special keys

Here is a table of the key binding sequences for the special keys.  Clink primarily uses VT220 emulation for keyboard input, but also uses some Xterm extended key sequences.

|           |Normal     |Shift       |Ctrl         |Ctrl+Shift   |Alt       |Alt+Shift   |Alt+Ctrl     |Alt+Ctrl+Shift|
|:-:        |:-:        |:-:         |:-:          |:-:          |:-:       |:-:         |:-:          |:-:           |
|Up         |`\e[A`     |`\e[1;2A`   |`\e[1;5A`    |`\e[1;6A`    |`\e[1;3A` |`\e[1;4A`   |`\e[1;7A`    |`\e[1;8A`     |
|Down       |`\e[B`     |`\e[1;2B`   |`\e[1;5B`    |`\e[1;6B`    |`\e[1;3B` |`\e[1;4B`   |`\e[1;7B`    |`\e[1;8B`     |
|Left       |`\e[D`     |`\e[1;2D`   |`\e[1;5D`    |`\e[1;6D`    |`\e[1;3D` |`\e[1;4D`   |`\e[1;7D`    |`\e[1;8D`     |
|Right      |`\e[C`     |`\e[1;2C`   |`\e[1;5C`    |`\e[1;6C`    |`\e[1;3C` |`\e[1;4C`   |`\e[1;7C`    |`\e[1;8C`     |
|Insert     |`\e[2~`    |`\e[2;2~`   |`\e[2;5~`    |`\e[2;6~`    |`\e[2;3~` |`\e[2;4~`   |`\e[2;7~`    |`\e[2;8~`     |
|Delete     |`\e[3~`    |`\e[3;2~`   |`\e[3;5~`    |`\e[3;6~`    |`\e[3;3~` |`\e[3;4~`   |`\e[3;7~`    |`\e[3;8~`     |
|Home       |`\e[H`     |`\e[1;2H`   |`\e[1;5H`    |`\e[1;6H`    |`\e[1;3H` |`\e[1;4H`   |`\e[1;7H`    |`\e[1;8H`     |
|End        |`\e[F`     |`\e[1;2F`   |`\e[1;5F`    |`\e[1;6F`    |`\e[1;3F` |`\e[1;4F`   |`\e[1;7F`    |`\e[1;8F`     |
|PgUp       |`\e[5~`    |`\e[5;2~`   |`\e[5;5~`    |`\e[5;6~`    |`\e[5;3~` |`\e[5;4~`   |`\e[5;7~`    |`\e[5;8~`     |
|PgDn       |`\e[6~`    |`\e[6;2~`   |`\e[6;5~`    |`\e[6;6~`    |`\e[6;3~` |`\e[6;4~`   |`\e[6;7~`    |`\e[6;8~`     |
|Tab        |`\t`       |`\e[Z`      |`\e[27;5;9~` |`\e[27;6;9~` | -        | -          | -           | -            |
|Space      |`Space`    | -          |`\e[27;5;32~`|`\e[27;6;32~`| -        | -          |`\e[27;7;32~`|`\e[27;8;32~` |
|Backspace  |`^h`       |`\e[27;2;8~`|`Rubout`     |`\e[27;6;8~` |`\e^h`    |`\e[27;4;8~`|`\eRubout`   |`\e[27;8;8~`  |
|F1         |`\eOP`     |`\e[1;2P`   |`\e[1;5P`    |`\e[1;6P`    |`\e\eOP`  |`\e\e[1;2P` |`\e\e[1;5P`  |`\e\e[1;6P`   |
|F2         |`\eOQ`     |`\e[1;2Q`   |`\e[1;5Q`    |`\e[1;6Q`    |`\e\eOQ`  |`\e\e[1;2Q` |`\e\e[1;5Q`  |`\e\e[1;6Q`   |
|F3         |`\eOR`     |`\e[1;2R`   |`\e[1;5R`    |`\e[1;6R`    |`\e\eOR`  |`\e\e[1;2R` |`\e\e[1;5R`  |`\e\e[1;6R`   |
|F4         |`\eOS`     |`\e[1;2S`   |`\e[1;5S`    |`\e[1;6S`    |`\e\eOS`  |`\e\e[1;2S` |`\e\e[1;5S`  |`\e\e[1;6S`   |
|F5         |`\e[15~`   |`\e[15;2~`  |`\e[15;5~`   |`\e[15;6~`   |`\e\e[15~`|`\e\e[15;2~`|`\e\e[15;5~` |`\e\e[15;6~`  |
|F6         |`\e[17~`   |`\e[17;2~`  |`\e[17;5~`   |`\e[17;6~`   |`\e\e[17~`|`\e\e[17;2~`|`\e\e[17;5~` |`\e\e[17;6~`  |
|F7         |`\e[18~`   |`\e[18;2~`  |`\e[18;5~`   |`\e[18;6~`   |`\e\e[18~`|`\e\e[18;2~`|`\e\e[18;5~` |`\e\e[18;6~`  |
|F8         |`\e[19~`   |`\e[19;2~`  |`\e[19;5~`   |`\e[19;6~`   |`\e\e[19~`|`\e\e[19;2~`|`\e\e[19;5~` |`\e\e[19;6~`  |
|F9         |`\e[20~`   |`\e[20;2~`  |`\e[20;5~`   |`\e[20;6~`   |`\e\e[20~`|`\e\e[20;2~`|`\e\e[20;5~` |`\e\e[20;6~`  |
|F10        |`\e[21~`   |`\e[21;2~`  |`\e[21;5~`   |`\e[21;6~`   |`\e\e[21~`|`\e\e[21;2~`|`\e\e[21;5~` |`\e\e[21;6~`  |
|F11        |`\e[23~`   |`\e[23;2~`  |`\e[23;5~`   |`\e[23;6~`   |`\e\e[23~`|`\e\e[23;2~`|`\e\e[23;5~` |`\e\e[23;6~`  |
|F12        |`\e[24~`   |`\e[24;2~`  |`\e[24;5~`   |`\e[24;6~`   |`\e\e[24~`|`\e\e[24;2~`|`\e\e[24;5~` |`\e\e[24;6~`  |

When the `terminal.differentiate_keys` setting is enabled then the following key bindings are also available:

|    |Ctrl           |Ctrl+Shift     |Alt            |Alt+Shift      |Alt+Ctrl       |Alt+Ctrl+Shift |
|:-: |:-:            |:-:            |:-:            |:-:            |:-:            |:-:            |
|`H` |`\e[27;5;72~`  |`\e[27;6;72~`  |`\eh`          |`\eH`          |`\e[27;7;72~`  |`\e[27;8;72~`  |
|`I` |`\e[27;5;73~`  |`\e[27;6;73~`  |`\ei`          |`\eI`          |`\e[27;7;73~`  |`\e[27;8;73~`  |
|`M` |`\e[27;5;77~`  |`\e[27;6;77~`  |`\em`          |`\eM`          |`\e[27;7;77~`  |`\e[27;8;77~`  |
|`[` |`\e[27;5;219~` |`\e[27;6;219~` |`\e[27;3;219~` |`\e[27;4;219~` |`\e[27;7;219~` |`\e[27;8;219~` |

The `terminal.raw_esc` setting controls the binding sequence for the <kbd>Esc</kbd> key:

|`terminal.raw_esc` Setting Value|Key Binding Sequence|
|:-|-|
|False (the default)|`\e[27;27~`|
|True (replicate Unix terminal input quirks and issues)|`\e`|

<a name="luakeybindings"></a>

### Lua Key Bindings

You can bind a key to a [Lua](#extending-clink) function by [binding](#keybindings) it to a macro that begins with "luafunc:".  Clink will invoke the named Lua function when the key binding is input.  Function names can include periods (such as `foo.bar`) but cannot include any other punctuation.

The Lua function receives a <a href="#rl_buffer">rl_buffer</a> argument that gives it access to the input buffer.

Lua functions can print output, but should first call <a href="#rl_buffer:beginoutput">rl_buffer:beginoutput</a> so that the output doesn't overwrite the displayed input line.

#### Basic example

Example of a Lua function key binding in a .inputrc file:

<pre><code class="plaintext">M-C-y:          <span class="hljs-string">"luafunc:insert_date"</span>
M-C-z:          <span class="hljs-string">"luafunc:print_date"</span>
</code></pre>

Example functions to go with that:

```lua
function insert_date(rl_buffer)
    rl_buffer:insert(os.date("%x %X"))
end

function print_date(rl_buffer)
    rl_buffer:beginoutput()
    print(os.date("%A %B %d, %Y   %I:%M %p"))
end
```

#### Advanced example

<a name="findlineexample"></a>

Here is an example that makes <kbd>F7</kbd>/<kbd>F8</kbd> jump to the previous/next screen line containing "error" or "warn" colored red or yellow, and makes <kbd>Shift</kbd>+<kbd>F7</kbd>/<kbd>Shift</kbd>+<kbd>F8</kbd> jump to the previous/next prompt line.

One way to use these is when reviewing compiler errors after building a project at the command line.  Press <kbd>Shift</kbd>+<kbd>F7</kbd> to jump to the previous prompt line, and then use <kbd>F8</kbd> repeatedly to jump to each next compiler warning or error listed on the screen.

Example key bindings for the .inputrc file:

<pre><code class="plaintext"><span class="hljs-string">"\e[18~"</span>:       <span class="hljs-string">"luafunc:find_prev_colored_line"</span>
<span class="hljs-string">"\e[19~"</span>:       <span class="hljs-string">"luafunc:find_next_colored_line"</span>
<span class="hljs-string">"\e[18;2~"</span>:     <span class="hljs-string">"luafunc:find_prev_prompt"</span>
<span class="hljs-string">"\e[19;2~"</span>:     <span class="hljs-string">"luafunc:find_next_prompt"</span>
</code></pre>

Example functions to go in a Lua script file:

```lua
#INCLUDE [examples\ex_findprompt.lua]

#INCLUDE [examples\ex_findline.lua]
```

## Saved Command History

Clink has a list of commands from the current session, and it can be saved and loaded across sessions.

A line won't be added to history if either of the following are true:
- The first word in the line matches one of the words in the `history.dont_add_to_history_cmds` setting.
- The line begins with a space character.

To prevent doskey alias expansion while still adding the line to history, you can start the line with a semicolon.

Line|Description
---|---
`somecmd`|Expands doskey alias and adds to history.
<code>&nbsp;somecmd</code>|Doesn't expand doskey alias and doesn't add to history.
`;somecmd`|Doesn't expand doskey alias but does add to history.

### The master history file

When the `history.saved` setting is enabled, then the command history is loaded and saved as follows (or when the setting is disabled, then it isn't saved between sessions).

Every time a new input line starts, Clink reloads the master history list and prunes it not to exceed the `history.max_lines` setting.

For performance reasons, deleting a history line marks the line as deleted without rewriting the history file.  When the number of deleted lines gets too large (exceeding the max lines or 200, whichever is larger) then the history file is compacted:  the file is rewritten with the deleted lines removed.

You can force the history file to be compacted regardless of the number of deleted lines by running `history compact`.

### Shared command history

When the `history.shared` setting is enabled, then all instances of Clink update the master history file and reload it every time a new input line starts.  This gives the effect that all instances of Clink share the same history -- a command entered in one instance will appear in other instances' history the next time they start an input line.

When the setting is disabled, then each instance of Clink loads the master file but doesn't append its own history back to the master file until after it exits, giving the effect that once an instance starts its history is isolated from other instances' history.

### Multiple master history files

Normally Clink saves a single saved master history list.  All instances of Clink load and save the same master history list.

It's also possible to make one or more instances of Clink use a different saved master history list by setting the `%CLINK_HISTORY_LABEL%` environment variable.  This can be up to 32 alphanumeric characters, and is appended to the master history file name.  Changing the `%CLINK_HISTORY_LABEL%` environment variable takes effect at the next input line.

## Sample Scripts

Here are a few samples of what can be done with Clink.

### clink-completions

The [clink-completions](https://github.com/vladimir-kotikov/clink-completions) collection of scripts has a bunch of argument matchers and completion generators for things like git, mercurial, npm, and more.

### cmder-powerline-prompt

The [cmder-powerline-prompt](https://github.com/chrisant996/cmder-powerline-prompt) collection of scripts provides a Powerline-like prompt for Clink.  It's extensible so you can add your own segments, and some configuration of built-in segments is also available.

### oh-my-posh

The [oh-my-posh](https://github.com/JanDeDobbeleer/oh-my-posh3) program can generate fancy prompts.  Refer to its documentation for how to configure it.

Integrating oh-my-posh with Clink is easy:  just save the following text to an `oh-my-posh.lua` file in your Clink scripts directory (run `clink info` to find that), and make sure the oh-my-posh.exe program is in a directory listed in the %PATH% environment variable.  (Or edit the script below to provide a fully qualified path to the oh-my-posh.exe program.)

```lua
-- oh-my-posh.lua
local ohmyposh_prompt = clink.promptfilter(1)
function ohmyposh_prompt:filter(prompt)
    prompt = io.popen("oh-my-posh.exe"):read("*a")
    return prompt, false
end
```

### z.lua

The [z.lua](https://github.com/skywind3000/z.lua) tool is a faster way to navigate directories, and it integrates with Clink.

## Upgrading from Clink v0.4.9

The new Clink tries to be as backward compatible with Clink v0.4.9 as possible. However, in some cases upgrading may require a little bit of configuration work.

- Some key binding sequences have changed; see [Key Bindings](#keybindings) for more information.
- Match coloring is now done by Readline and is configured differently; see [Completion Colors](#completioncolors) for more information.
- Settings and history should migrate automatically if the new `clink_settings` and `clink_history` files don't exist (deleting them will cause migration to happen again).  To find the directory that contains these files, run `clink info` and look for the "state" line.
- Script compatibility should be very good, but some scripts may still encounter problems.  If you do encounter a compatibility problem you can look for an updated version of the script, update the script yourself, or visit the <a href="https://github.com/chrisant996/clink/issues">repo</a> and open an issue describing details about the compatibility problem.
- Some settings have changed slightly, and there are many new settings.  See [Configuring Clink](#configclink) for more information.

## Troubleshooting Tips

If something seems to malfunction, here are some things to try that often help track down what's going wrong:

- Check if anti-malware software blocked Clink from injecting.  Consider adding an exclusion for Clink.
- Check `clink info`.  E.g. does the state dir look right, do the script paths look right, do the inputrc files look right?
- Check `clink set`.  E.g. do the settings look right?
- Check the `clink.log` file for clues (its location is reported by `clink info`).

When [reporting an issue](https://github.com/chrisant996/clink/issues), please include the following which answer a bunch of commonly relevant questions and saves time:

- Please describe what was expected to happen.
- Please describe what actually happened.
- Please include the output from `clink info` and `clink set`.
- Please include the `clink.log` file (the location is reported by `clink info`).

