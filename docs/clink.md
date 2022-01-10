# What is Clink?

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

### Features

Here are some highlights of what Clink provides:

- The same line editing as Bash (from the [GNU Readline library](https://tiswww.case.edu/php/chet/readline/rltop.html) version 8.1).
- History persistence between sessions.
- Context sensitive completion;
  - Executables (and aliases).
  - Directory commands.
  - Environment variables.
- Context sensitive colored input text.
- Automatic suggestions from history and completions.
- New keyboard shortcuts;
  - Interactive completion list (<kbd>Ctrl</kbd>+<kbd>Space</kbd>).
  - Incremental history search (<kbd>Ctrl</kbd>+<kbd>R</kbd> and <kbd>Ctrl</kbd>+<kbd>S</kbd>).
  - Powerful completion (<kbd>Tab</kbd>).
  - Undo (<kbd>Ctrl</kbd>+<kbd>Z</kbd>).
  - Environment variable expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>E</kbd>).
  - Doskey alias expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd>).
  - Scroll the screen buffer (<kbd>Alt</kbd>+<kbd>Up</kbd>, etc).
  - <kbd>Shift</kbd>+Arrow keys to select text, typing replaces selected text, etc.
  - (press <kbd>Alt</kbd>+<kbd>H</kbd> for many more...)
- Directory shortcuts;
  - Typing a directory name followed by a path separator is a shortcut for `cd /d` to that directory.
  - Typing `..` or `...` is a shortcut for `cd ..` or `cd ..\..` (each additional `.` adds another `\..`).
  - Typing `-` or `cd -` changes to the previous current working directory.
- Scriptable autosuggest with Lua.
- Scriptable completion with Lua.
- Scriptable key bindings with Lua.
- Colored and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds <kbd>Alt</kbd>+<kbd>H</kbd> to display the current key bindings. More features can also be found in GNU's [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html).

# Usage

There are several ways to start Clink.

1. If you installed the auto-run, just start `cmd.exe`.  Run `clink autorun --help` for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `<install_dir>\clink.exe inject`.

Starting Clink injects it into a `cmd.exe` process, where it intercepts a handful of Windows API functions so that it can replace the prompt and input line editing with its own Readline-powered enhancements.

# Getting Started

You can use Clink right away without configuring anything:

- Searchable [command history](#saved-command-history) will be saved between sessions.
- <kbd>Tab</kbd> and <kbd>Ctrl</kbd>+<kbd>Space</kbd> will do match completion two different ways.
- Press <kbd>Alt</kbd>+<kbd>H</kbd> to see a list of the current key bindings.
- Press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>/</kbd> followed by another key to see what command is bound to the key.

There are three main ways of customizing Clink to your preferences:  the [Readline init file](#init-file) (the `.inputrc` file), the [Clink settings](#clink-settings) (the `clink set` command), and [Lua](#extending-clink-with-lua) scripts.

## Common Configuration

The following sections describe some ways to begin customizing Clink to your taste.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_inputrc">Create a .inputrc file</a></td><td class="lmtd">Create a .inputrc file where config variables and key bindings can be set.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_defaultbindings">Bash vs Windows</a></td><td class="lmtd">Make <kbd>Ctrl</kbd>+<kbd>F</kbd> and <kbd>Ctrl</kbd>+<kbd>M</kbd> work like usual on Windows.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_autosuggest">Auto-suggest</a></td><td class="lmtd">How to enable and use automatic suggestions.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_colors">Colors</a></td><td class="lmtd">Configure the colors.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_keybindings">Key Bindings</a></td><td class="lmtd">Customize your key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_startupcmdscript">Startup Cmd Script</a></td><td class="lmtd">Optional automatic <code>clink_startup.cmd</code> script.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_customprompt">Custom Prompt</a></td><td class="lmtd">Customizing the command line prompt.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#upgradefrom049">Upgrading from Clink v0.4.9</a></td><td class="lmtd">Notes on upgrading from a very old version of Clink.</td></tr>
</table>

<a name="gettingstarted_inputrc"></a>

### Create a .inputrc file

First you'll want to create a `.inputrc` file, and a good place is in your Windows user profile directory.

This file is used for some configuration, such as key bindings and colored completions.

Create the file by running this command at a CMD prompt:

```cmd
notepad %userprofile%\.inputrc
```

You may want to copy/paste the following sample text into the file as a starting point, and then press <kbd>Ctrl</kbd>+<kbd>S</kbd> to save the file.

```plaintext
# Some common Readline config settings.

set colored-stats                 on   # Turn on completion colors.
set colored-completion-prefix     on   # Color the typed completion prefix.

# Some config settings that only work in Clink.

$if clink
set search-ignore-case            on   # Case insensitive history searches.
set completion-auto-query-items   on   # Prompt before showing completions if they'll exceed half the screen.
$endif

# Add your keybindings here...
```

<a name="gettingstarted_defaultbindings"></a>

### Bash vs Windows

The default Clink key bindings are the same as in the "bash" shell for Unix/Linux.  That makes some keys like <kbd>Ctrl</kbd>+<kbd>F</kbd> and <kbd>Ctrl</kbd>+<kbd>M</kbd> behave differently than you might be used to in CMD.

To instead use the familiar [Windows default key bindings](#default_bindings) you can run `clink set clink.default_bindings windows`.

<a name="gettingstarted_autosuggest"></a>

### Auto-suggest

Clink can suggest commands as you type, based on command history and completions.

To turn on automatic suggestions, run `clink set autosuggest.enable true`.  When the cursor is at the end of the input line, a suggestion may appear in a muted color.  If the suggestion isn't what you want, just ignore it.  Or accept the whole suggestion with the <kbd>Right</kbd> arrow or <kbd>End</kbd> key, accept the next word of the suggestion with <kbd>Ctrl</kbd>+<kbd>Right</kbd>, or accept the next full word of the suggestion up to a space with <kbd>Shift</kbd>+<kbd>Right</kbd>.

The [`autosuggest.strategy`](#autosuggest_strategy) setting determines how a suggestion is chosen.

<a name="gettingstarted_colors"></a>

### Colors

Clink has many configurable colors for match completions, input line coloring, popup lists, and more.

#### Completion colors

When performing completion (e.g. <kbd>Tab</kbd> or <kbd>Ctrl</kbd>+<kbd>Space</kbd>) Clink can add color to the possible completions.

To turn on colored completions, put a line `set colored-stats on` in the [.inputrc file](#gettingstarted_inputrc) (if you copy/pasted the sample text, it's already there).

See the [Completion Colors](#completion-colors) section for how to configure the colors for match completions.

#### Other colors

Clink adds color to the input line by highlighting arguments, flags, doskey macros, and more.  If you don't want input line colors, you can turn it off by running `clink set clink.colorize_input false`.

There are also colors for popup lists, and some other things.

To configure a color, run <code>clink set <span class="arg">colorname</span> <span class="arg">colorvalue</span></code>.

Match completions make it easy to change Clink settings:  type <code>clink set color.</code> and then use completion (e.g. <kbd>Tab</kbd> or <kbd>Ctrl</kbd>+<kbd>Space</kbd>) to see the available color settings, and to fill in a color value.

See the [Clink Settings](#clink-settings) and [Color Settings](#color-settings) sections for more information on Clink settings.

<a name="gettingstarted_keybindings"></a>

### Key Bindings

You can customize your key bindings (keyboard shortcuts) by assigning key bindings in the [.inputrc file](#gettingstarted_inputrc).  See [Customizing Key Bindings](#keybindings) for more information.

Clink comes with many pre-configured key bindings that invoke named commands.  Here are a few that you might find especially handy:

<table>
<tr><td><kbd>Alt</kbd>+<kbd>H</kbd></td><td>This is <code>clink-show-help</code>, which lists the key bindings and commands.  Learn more by visiting <a href="#keybindings">Customizing Key Bindings</a>.</td></tr>
<tr><td><kbd>Tab</kbd></td><td>This is <code>complete</code> or <code>old-menu-complete</code>, depending on the [`clink.default_bindings`](#default_bindings) Clink setting.  <code>complete</code> swhich performs completion by selecting from an interactive list of possible completions; if there is only one match, the match is inserted immediately.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Space</kbd></td><td>This is <code>clink-select-complete</code>, which performs completion by selecting from an interactive list of possible completions; if there is only one match, the match is inserted immediately.</td></tr>
<tr><td><kbd>Alt</kbd>+<kbd>=</kbd></td><td>This is <code>possible-completions</code>, which lists the available completions for the current word in the input line.</td></tr>
<tr><td><kbd>Alt</kbd>+<kbd>.</kbd></td><td>This is <code>yank-last-arg</code>, which inserts the last argument from the previous line.  You can use it repeatedly to cycle backwards through the history, inserting the last argument from each line.  Learn more by reading up on the "yank" features in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html).</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>R</kbd></td><td>This is <code>reverse-search-history</code>, which incrementally searches the history.  Press it, then type, and it does a reverse incremental search while you type.  Press <kbd>Ctrl</kbd>+<kbd>R</kbd> again (and again, etc) to search for other matches of the search text.  Learn more by reading up on the "search" and "history" features in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html).</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>D</kbd></td><td>This is <code>remove-history</code>, which deletes the currently selected history line after using any of the history search or navigation commands.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>K</kbd></td><td>This is <code>add-history</code>, which adds the current line to the history without executing it, and then clears the input line.</td></tr>
<tr><td><kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>N</kbd></td><td>This is <code>clink-menu-complete-numbers</code>, which grabs numbers with 3 or more digits from the current console screen and cycles through inserting them as completions (binary, octal, decimal, hexadecimal).  Super handy for quickly inserting a commit hash that was printed as output from a preceding command.</td></tr>
<tr><td><kbd>Alt</kbd>+<kbd>0</kbd> to <kbd>Alt</kbd>+<kbd>9</kbd></td><td>These are <code>digit-argument</code>, which let you enter a numeric value used by many commands.  For example <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>W</kbd> copies the current word to the clipboard, but if you first type <kbd>Alt</kbd>+<kbd>2</kbd> followed by <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>W</kbd> then it copies the 3rd word to the clipboard (the first word is 0, the second is 1, etc).  Learn more by reading up on "Readline Arguments" in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html).</td></tr>
</table>

For a full list of commands available for key bindings, see [New Commands](#new-commands) and "Bindable Readline Commands" in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html).

<a name="gettingstarted_startupcmdscript"></a>

### Startup Cmd Script

When Clink is injected, it looks for a `clink_start.cmd` script in the binaries directory and [profile directory](#filelocations).  Clink automatically runs the script(s), if present, when the first CMD prompt is shown after Clink is injected.  You can set the [`clink.autostart`](#clink_autostart) setting to run a different command, or set it to "nul" to run no command at all.

<a name="gettingstarted_customprompt"></a>

### Custom Prompt

If you want a customizable prompt with a bunch of styles and an easy-to-use configuration wizard, check out <a href="https://github.com/chrisant996/clink-flex-prompt">clink-flex-prompt</a>.  Also, if you've been disappointed by git making the prompt slow in other shells, try this prompt -- it makes the prompt appear instantly by running git commands in the background and refreshing the prompt once the background commands complete.

Other popular configurable prompts are [oh-my-posh](#oh-my-posh) and [starship](#starship).

See [Customizing the Prompt](#customisingtheprompt) for information on how to use Lua to customize the prompt.

<a name="upgradefrom049"></a>

## Upgrading from Clink v0.4.9

The new Clink tries to be as backward compatible with Clink v0.4.9 as possible. However, in some cases upgrading may require a little bit of configuration work.

- Some key binding sequences have changed; see [Customizing Key Bindings](#keybindings) for more information.
- Match coloring works differently now and can do much more; see [Completion Colors](#completioncolors) for more information.
- Old settings and history migrate automatically if the new `clink_settings` and `clink_history` files don't exist (deleting them will cause migration to happen again).  To find the directory that contains these files, run `clink info` and look for the "state" line.
- Script compatibility should be very good, but some scripts may still encounter problems.  If you do encounter a compatibility problem you can look for an updated version of the script, update the script yourself, or visit the [clink repo](https://github.com/chrisant996/clink/issues) and open an issue describing details about the compatibility problem.
- Some settings have changed slightly, and there are many new settings.  See [Configuring Clink](#configclink) for more information.

<a name="configclink"></a>

# Configuring Clink

Clink has two configuration systems, which is a result of using the Readline library to provide command history and key bindings.

The following sections describe how to configure Clink itself.  To learn about the Readline configuration and key bindings, instead see [Configuring Readline](#configreadline).

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#clinksettings">Clink Settings</a></td><td class="lmtd">How to customize Clink's settings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#colorsettings">Color Settings</a></td><td class="lmtd">Describes the syntax used by color settings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#filelocations">File Locations</a></td><td class="lmtd">Where Clink stores its history and settings files.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#command-line-options">Command Line Options</a></td><td class="lmtd">Describes the command line options for the Clink program.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#portable-configuration">Portable Configuration</a></td><td class="lmtd">How to set up a "portable" installation of Clink, e.g. on a USB drive or network location.</td></tr>
</table>

<a name="clinksettings"></a>

## Clink Settings

The easiest way to configure Clink is to use the `clink set` command.  This can list, query, and set Clink's settings. Run `clink set --help` from a Clink-installed cmd.exe process to learn more both about how to use it and to get descriptions for Clink's various options.

The following table describes the available Clink settings:

Name                         | Default | Description
:--:                         | :-:     | -----------
`autosuggest.async`          | True    | When this is <code>true</code> matches are generated asynchronously for suggestions.  This helps to keep typing responsive.
<a name="autosuggest_enable"></a>`autosuggest.enable` | False | When this is `true` a suggested command may appear in `color.suggestion` color after the cursor.  If the suggestion isn't what you want, just ignore it.  Or accept the whole suggestion with the <kbd>Right</kbd> arrow or <kbd>End</kbd> key, accept the next word of the suggestion with <kbd>Ctrl</kbd>+<kbd>Right</kbd>, or accept the next full word of the suggestion up to a space with <kbd>Shift</kbd>+<kbd>Right</kbd>.  The `autosuggest.strategy` setting determines how a suggestion is chosen.
`autosuggest.original_case`  | True | When this is enabled (the default), accepting a suggestion uses the original capitalization from the suggestion.
<a name="autosuggest_strategy"></a>`autosuggest.strategy` | `match_prev_cmd history completion` | This determines how suggestions are chosen.  The suggestion generators are tried in the order listed, until one provides a suggestion.  There are three built-in suggestion generators, and scripts can provide new ones.  `history` chooses the most recent matching command from the history.  `completion` chooses the first of the matching completions.  `match_prev_cmd` chooses the most recent matching command whose preceding history entry matches the most recently invoked command, but only when the `history.dupe_mode` setting is `add`.
<a name="clink_autostart"></a>`clink.autostart` | | This command is automatically run when the first CMD prompt is shown after Clink is injected.  If this is blank (the default), then Clink instead looks for `clink_start.cmd` in the binaries directory and profile directory and runs them.  Set it to "nul" to not run any autostart command.
`clink.colorize_input`       | True    | Enables context sensitive coloring for the input text (see [Coloring the Input Text](#classifywords)).
<a name="default_bindings"></a>`clink.default_bindings` | `bash` | Clink uses bash key bindings when this is set to `bash` (the default).  When this is set to `windows` Clink overrides some of the bash defaults with familiar Windows key bindings for <kbd>Tab</kbd>, <kbd>Ctrl</kbd>+<kbd>A</kbd>, <kbd>Ctrl</kbd>+<kbd>F</kbd>, <kbd>Ctrl</kbd>+<kbd>M</kbd>, and <kbd>Right</kbd>.
`clink.gui_popups`           | False   | When set, Clink uses GUI popup windows instead console text popups.  The `color.popup` settings have no effect on GUI popup windows.
`clink.logo`                 | `full`  | Controls what startup logo to show when Clink is injected.  `full` = show full copyright logo, `short` = show abbreviated version info, `none` = omit the logo.
`clink.paste_crlf`           | `crlf`  | What to do with CR and LF characters on paste. Setting this to `delete` deletes them, `space` replaces them with spaces, `ampersand` replaces them with ampersands, and `crlf` pastes them as-is (executing commands that end with a newline).
`clink.path`                 |         | A list of paths from which to load Lua scripts. Multiple paths can be delimited semicolons.
`clink.promptfilter`         | True    | Enable [prompt filtering](#customising-the-prompt) by Lua scripts.
`cmd.auto_answer`            | `off`   | Automatically answers cmd.exe's "Terminate batch job (Y/N)?" prompts. `off` = disabled, `answer_yes` = answer Y, `answer_no` = answer N.
`cmd.ctrld_exits`            | True    | <kbd>Ctrl</kbd>+<kbd>D</kbd> exits the process when it is pressed on an empty line.
`cmd.get_errorlevel`         | True    | When this is enabled, Clink runs a hidden `echo %errorlevel%` command before each interactive input prompt to retrieve the last exit code for use by Lua scripts.  If you experience problems, try turning this off.  This is on by default.
`color.arg`                  |         | The color for arguments in the input line when `clink.colorize_input` is enabled.
`color.arginfo`              | `yellow` | Argument info color.  Some argmatchers may show that some flags or arguments accept additional arguments, when listing possible completions.  This color is used for those additional arguments.  (E.g. the "dir" in a "-x dir" listed completion.)
`color.argmatcher`           |         | The color for the command name in the input line when `clink.colorize_input` is enabled, if the command name has an argmatcher available.
<a name="color_cmd"></a>`color.cmd` | `bold` | Used when displaying shell (CMD.EXE) command completions, and in the input line when `clink.colorize_input` is enabled.
`color.cmdredir`             | `bold`  | The color for redirection symbols (`<`, `>`, `>&`) in the input line when `clink.colorize_input` is enabled.
`color.cmdsep`               | `bold`  | The color for command separaors (`&`, `|`) in the input line when `clink.colorize_input` is enabled.
`color.comment_row`          | `bright white on cyan` | The color for the comment row in the `clink-select-complete` command.  The comment row shows the "and <em>N</em> more matches" or "rows <em>X</em> to <em>Y</em> of <em>Z</em>" messages.
`color.description`          | `bright cyan` | Used when displaying descriptions for match completions.
<a name="color_doskey"></a>`color.doskey` | `bright cyan` | Used when displaying doskey alias completions, and in the input line when `clink.colorize_input` is enabled.
<a name="color_filtered"></a>`color.filtered` | `bold` | The default color for filtered completions (see [Filtering the Match Display](#filteringthematchdisplay)).
`color.flag`                 | `default` | The color for flags in the input line when `clink.colorize_input` is enabled.
<a name="color_hidden"></a>`color.hidden` | | Used when displaying file completions with the "hidden" attribute.
`color.horizscroll`          |         | The color for the `<` or `>` horizontal scroll indicators when Readline's `horizontal-scroll-mode` variable is set.
`color.input`                |         | The color for input line text. Note that when `clink.colorize_input` is disabled, the entire input line is displayed using `color.input`.
`color.interact`             | `bold`  | The color for prompts such as a pager's `--More?--` prompt.
`color.message`              | `default` | The color for the message area (e.g. the search prompt message, digit argument prompt message, etc).
`color.popup`                |         | When set, this is used as the color for popup lists and messages.  If no color is set, then the console's popup colors are used (see the Properties dialog box for the console window).
`color.popup_desc`           |         | When set, this is used as the color for description column(s) in popup lists.  If no color is set, then a color is chosen to complement the console's popup colors (see the Properties dialog box for the console window).
`color.prompt`               |         | When set, this is used as the default color for the prompt.  But it's overridden by any colors set by [Customizing The Prompt](#customisingtheprompt).
<a name="color_readonly"></a>`color.readonly` | | Used when displaying file completions with the "readonly" attribute.
`color.selected_completion`  |         | The color for the selected completion with the clink-select-complete command.  If no color is set, then bright reverse video is used.
`color.selection`            |         | The color for selected text in the input line.  If no color is set, then reverse video is used.
<a name="color_suggestion"></a>`color.suggestion` | `bright black` | The color for automatic suggestions when `autosuggest.enable` is enabled.
`color.unexpected`           | `default` | The color for unexpected arguments in the input line when `clink.colorize_input` is enabled.
`debug.log_terminal`         | False   | Logs all terminal input and output to the clink.log file.  This is intended for diagnostic purposes only, and can make the log file grow significantly.
`doskey.enhanced`            | True    | Enhanced Doskey adds the expansion of macros that follow `\|` and `&` command separators and respects quotes around words when parsing `$1`...`$9` tags. Note that these features do not apply to Doskey use in Batch files.
`exec.aliases`               | True    | When matching executables as the first word (`exec.enable`), include doskey aliases.
`exec.commands`              | True    | When matching executables as the first word (`exec.enable`), include CMD commands (such as `cd`, `copy`, `exit`, `for`, `if`, etc).
`exec.cwd`                   | True    | When matching executables as the first word (`exec.enable`), include executables in the current directory. (This is implicit if the word being completed is a relative path, or if `exec.files` is true.)
`exec.dirs`                  | True    | When matching executables as the first word (`exec.enable`), also include directories relative to the current working directory as matches.
`exec.enable`                | True    | Match executables when completing the first word of a line.  Executables are determined by the extensions listed in the `%PATHEXT%` environment variable.
`exec.files`                 | False   | When matching executables as the first word (`exec.enable`), include files in the current directory.
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
<a name="lua_debug"></a>`lua.debug` | False | Loads a simple embedded command line debugger when enabled. Breakpoints can be added by calling [pause()](#pause).
`lua.path`                   |         | Value to append to `package.path`. Used to search for Lua scripts specified in `require()` statements.
<a name="lua_reload_scripts"></a>`lua.reload_scripts` | False | When false, Lua scripts are loaded once and are only reloaded if forced (see [The Location of Lua Scripts](#lua-scripts-location) for details).  When true, Lua scripts are loaded each time the edit prompt is activated.
`lua.strict`                 | True    | When enabled, argument errors cause Lua scripts to fail.  This may expose bugs in some older scripts, causing them to fail where they used to succeed. In that case you can try turning this off, but please alert the script owner about the issue so they can fix the script.
`lua.traceback_on_error`     | False   | Prints stack trace on Lua errors.
`match.expand_envvars`       | False   | Expands environment variables in a word before performing completion.
`match.ignore_accent`        | True    | Controls accent sensitivity when completing matches. For example, `ä` and `a` are considered equivalent with this enabled.
`match.ignore_case`          | `relaxed` | Controls case sensitivity when completing matches. `off` = case sensitive, `on` = case insensitive, `relaxed` = case insensitive plus `-` and `_` are considered equal.
`match.max_rows`             | `0`     | The maximum number of rows that `clink-select-complete` can use.  When this is 0, the limit is the terminal height.
`match.preview_rows`         | `0`     | The number of rows to show as a preview when using the `clink-select-complete` command (bound by default to <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Space</kbd>).  When this is 0, all rows are shown and if there are too many matches it instead prompts first like the `complete` command does.  Otherwise it shows the specified number of rows as a preview without prompting, and it expands to show the full set of matches when the selection is moved past the preview rows.
`match.sort_dirs`            | `with`  | How to sort matching directory names. `before` = before files, `with` = with files, `after` = after files.
`match.translate_slashes`    | `system` | File and directory completions can be translated to use consistent slashes.  The default is `system` to use the appropriate path separator for the OS host (backslashes on Windows).  Use `slash` to use forward slashes, or `backslash` to use backslashes.  Use `off` to turn off translating slashes from custom match generators.
`match.wild`                 | True    | Matches `?` and `*` wildcards and leading `.` when using any of the completion commands.  Turn this off to behave how bash does, and not match wildcards or leading dots (but `glob-complete-word` always matches wildcards).
`prompt.async`               | True    | Enables [asynchronous prompt refresh](#asyncpromptfiltering).  Turn this off if prompt filter refreshes are annoying or cause problems.
<a name="prompt-transient"></a>`prompt.transient` | `off` | Controls when past prompts are collapsed ([transient prompts](#transientprompts)).  `off` = never collapse past prompts, `always` = always collapse past prompts, `same_dir` = only collapse past prompts when the current working directory hasn't changed since the last prompt.
`readline.hide_stderr`       | False   | Suppresses stderr from the Readline library.  Enable this if Readline error messages are getting in the way.
`terminal.adjust_cursor_style`| True   | When enabled, Clink adjusts the cursor shape and visibility to show Insert Mode, produce the visible bell effect, avoid disorienting cursor flicker, and to support ANSI escape codes that adjust the cursor shape and visibility. But it interferes with the Windows 10 Cursor Shape console setting. You can make the Cursor Shape setting work by disabling this Clink setting (and the features this provides).
`terminal.differentiate_keys`| False   | When enabled, pressing <kbd>Ctrl</kbd> + <kbd>H</kbd> or <kbd>I</kbd> or <kbd>M</kbd> or <kbd>[</kbd> generate special key sequences to enable binding them separately from <kbd>Backspace</kbd> or <kbd>Tab</kbd> or <kbd>Enter</kbd> or <kbd>Esc</kbd>.
`terminal.east_asian_ambiguous`|`auto` | There is a group of East Asian characters whose widths are ambiguous in the Unicode standard.  This setting controls how to resolve the ambiguous widths.  By default this is set to `auto`, but some terminal hosts may require setting this to a different value to work around limitations in the terminal hosts.  Setting this to `font` measures the East Asian Ambiguous character widths using the current font.  Setting it to `one` uses 1 as the width, or `two` uses 2 as the width.  When this is `auto` (the default) and the current code page is 932, 936, 949, or 950 then the current font is used to measure the widths, or for any other code pages (including UTF8) the East Asian Ambiguous character widths are assumed to be 1.
`terminal.emulation`         | `auto`  | Clink can either emulate a virtual terminal and handle ANSI escape codes itself, or let the console host natively handle ANSI escape codes. `native` = pass output directly to the console host process, `emulate` = clink handles ANSI escape codes itself, `auto` = emulate except when running in ConEmu, Windows Terminal, or Windows 10 new console.
`terminal.raw_esc`           | False | When enabled, pressing Esc sends a literal escape character like in Unix/etc terminals.  This setting is disabled by default to provide a more predictable, reliable, and configurable input experience on Windows.  Changing this only affects future Clink sessions, not the current session.
`terminal.use_altgr_substitute`| False | Support Windows' <kbd>Ctrl</kbd>-<kbd>Alt</kbd> substitute for <kbd>AltGr</kbd>. Turning this off may resolve collisions with Readline's key bindings.

<p/>

> **Compatibility Notes:**
> - The `esc_clears_line` setting has been replaced by a `clink-reset-line` command that is by default bound to the <kbd>Escape</kbd> key.  See [Customizing Key Bindings](#keybindings) and [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html) for more information.
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

It's also possible to set any ANSI [SGR escape code](https://en.wikipedia.org/wiki/ANSI_escape_code#SGR) using <code>sgr <span class="arg">SGR_parameters</span></code> (for example `sgr 7` is the code for reverse video, which swaps the foreground and background colors).

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
<code>clink set --describe</code> by itself lists all settings and their descriptions (instead of their values).<br/>
<code>clink set <span class="arg">setting_name</span></code> describes the setting and shows its current value.<br/>
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

1. Put your Lua scripts and other tools in the same directory as the Clink executable files.  For example fzf.exe, z.cmd, oh-my-posh.exe, starship.exe etc can all go in the same directory on a flash drive or network share.
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

The Readline library used by Clink can be configured to add custom keybindings and macros by creating a Readline init file. There is excellent documentation for all the options and commands available to configure Readline in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html).

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#init-file">Init File</a></td><td class="lmtd">About the .inputrc init file.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#completion-colors">Completion Colors</a></td><td class="lmtd">How to customize the completion colors.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#popupwindow">Popup Windows</a></td><td class="lmtd">Using the popup windows.</td></tr>
</table>

## Init File

A Readline init file defines key binding customizations and sets Readline configuration variables.

A Readline init file is named `.inputrc` or `_inputrc`.  Clink searches the directories referenced by the following environment variables in the order listed here, and loads the first `.inputrc` or `_inputrc` file it finds:
- `%CLINK_INPUTRC%`
- The Clink profile directory (see the "state" line from `clink info`; by default this is the same as `%USERPROFILE%` but it can be overridden by the `clink inject` command).
- `%USERPROFILE%`
- `%LOCALAPPDATA%`
- `%APPDATA%`
- `%HOME%`

Other software that also uses the Readline library will also look for the `.inputrc` file (and possibly the `_inputrc` file too). To set macros and keybindings intended only for Clink, one can use the Readline init file conditional construct like this; `$if clink [...] $endif`.

You can use `clink info` to find the directories and configuration file for the current Clink session.

> **Compatibility Notes:**
> - The `clink_inputrc_base` file from v0.4.8 is no longer used.
> - For backward compatibility, `clink_inputrc` is also loaded from the above locations, but it has been deprecated and may be removed in the future.
> - Clink v1.0.0a0 through Clink v1.2.27 accidentally loaded up to one Readline init file from each of the searched directories. That was incorrect behavior for loading Readline init files and has been fixed. If similar behavior is still desired, consider using the `$include` directive in the Readline init file, to load additional files.

### Basic Format

The `.inputrc` file will mostly use these kinds of lines:

<table>
<tr><th>Line</th><th>Description</th></tr>
<tr><td><code><span class="arg">keyname</span>: <span class="arg">command</span></code></td><td>Binds a named command to a key.</td></tr>
<tr><td><code><span class="arg">keyname</span>: "<span class="arg">literal text</span>"</code></td><td>Binds a macro to a key.  A macro inserts the literal text into the input line.</td></tr>
<tr><td><code><span class="arg">keyname</span>: "luafunc:<span class="arg">lua_function_name</span>"</code></td><td>Binds a named Lua function to a key.  See <a href="#luakeybindings">Lua key bindings</a> for more information.</td></tr>
<tr><td><code>set <span class="arg">varname</span> <span class="arg">value</span></code></td><td></td></tr>
</table>

See [Discovering Clink key sequences](#discovering-clink-key-sequences) for how to find the <span class="arg">keyname</span> for the key you want to bind.  See [Customizing Key Bindings](#keybindings) for more information about binding keys in Clink.

Refer to the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html) for a more thorough explanation of the .inputrc file format, list of available commands, and list of configuration variables and their values.

### New Configuration Variables

Clink adds some new configuration variables to Readline, beyond what's described in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html):

Name | Default | Description
:-:|:-:|---
`completion-auto-query-items`|on|Automatically prompts before displaying completions if they need more than half a screen page.
`history-point-at-end-of-anchored-search`|off|Puts the cursor at the end of the line when using `history-search-forward` or `history-search-backward`.
`menu-complete-wraparound`|on|The `menu-complete` family of commands wraps around when reaching the end of the possible completions.
`search-ignore-case`|on|Controls whether the history search commands ignore case.

### New Commands

Clink adds some new commands to Readline, beyond what's described in the [Readline manual](https://tiswww.cwru.edu/php/chet/readline/rluserman.html):

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
`clink-expand-history`|Perform [history](#using-history-expansion) expansion in the current input line.
`clink-expand-history-and-alias`|Perform [history](#using-history-expansion) and doskey alias expansion in the current input line.
`clink-expand-line`|Perform [history](#using-history-expansion), doskey alias, and environment variable expansion in the current input line.
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
`clink-selectall-conhost`|Mimics the "Select All" command when running in a standard console window (hosted by the OS conhots).  Selects the input line text.  If already selected, then it invokes the "Select All" command from the console window's system menu and selects the entire screen buffer's contents.
`clink-show-help`|Lists the currently active key bindings using friendly key names.  A numeric argument affects showing categories and descriptions:  0 for neither, 1 for categories, 2 for descriptions, 3 for categories and descriptions (the default), 4 for all commands (even if not bound to a key).
`clink-show-help-raw`|Lists the currently active key bindings using raw key sequences.  A numeric argument affects showing categories and descriptions:  0 for neither, 1 for categories, 2 for descriptions, 3 for categories and descriptions (the default), 4 for all commands (even if not bound to a key).
`clink-up-directory`|Changes to the parent directory.
`clink-what-is`|Show the key binding for the next key sequence that is input.
`cua-backward-char`|Extends the selection and moves back a character.
`cua-backward-word`|Extends the selection and moves back a word.
`cua-beg-of-line`|Extends the selection and moves to the start of the current line.
`cua-copy`|Copies the selection to the clipboard.
`cua-cut`|Cuts the selection to the clipboard.
`cua-end-of-line`|Extends the selection and moves to the end of the line.
`cua-forward-char`|Extends the selection and moves forward a character, or inserts the next full suggested word up to a space.
`cua-forward-word`|Extends the selection and moves forward a word.
`cua-select-all`|Extends the selection to the entire current line.
`edit-and-execute-command`|Invoke an editor on the current input line, and execute the result as commands.  This attempts to invoke %VISUAL%, %EDITOR%, or notepad.exe as the editor, in that order.
`glob-complete-word`|Perform wildcard completion on the text before the cursor point, with a `*` implicitly appended.
`glob-expand-word`|Insert all the wildcard completions that `glob-list-expansions` would list.  If a numeric argument is supplied, a `*` is implicitly appended before completion.
`glob-list-expansions`|List the possible wildcard completions of the text before the cursor point.  If a numeric argument is supplied, a `*` is implicitly appended before completion.
`history-and-alias-expand-line`|A synonym for `clink-expand-history-and-alias`.
`history-expand-line`|A synonym for `clink-expand-history`.
`insert-last-argument`|A synonym for `yank-last-arg`.
`magic-space`|Perform [history](#using-history-expansion) expansion on the text before the cursor position and insert a space.
`old-menu-complete-backward`|Like `old-menu-complete`, but in reverse.
`remove-history`|While searching history, removes the current line from the history.
`shell-expand-line`|A synonym for `clink-expand-line`.
`win-copy-history-number`|Enter a history number and replace the input line with the history line (mimics Windows console <kbd>F9</kbd>).
`win-copy-up-to-char`|Enter a character and copy up to it from the previous command (mimics Windows console <kbd>F2</kbd>).
`win-copy-up-to-end`|Copy the rest of the previous command (mimics Windows console <kbd>F3</kbd>).
`win-cursor-forward`|Move cursor forward, or at end of line copy character from previous command, or insert suggestion (mimics Windows console <kbd>F1</kbd> and <kbd>Right</kbd>).
`win-delete-up-to-char`|Enter a character and delete up to it in the input line (mimics Windows console <kbd>F4</kbd>).
`win-history-list`|Executes a history entry from a list (mimics Windows console <kbd>F7</kbd>).
`win-insert-eof`|Insert ^Z (mimics Windows console <kbd>F6</kbd>).

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

## Popup Windows

Some commands show a searchable popup window that lists the available completions, directory history, or command history.

For example, `win-history-list` (<kbd>F7</kbd>) and `clink-popup-directories` (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>PgUp</kbd>) show popup windows.

Here's how the popup windows work:

Key | Description
:-:|---
<kbd>Escape</kbd>|Cancels the popup.
<kbd>Enter</kbd>|Inserts the highlighted completion, changes to the highlighted directory, or executes the highlighted command.
<kbd>Shift</kbd>+<kbd>Enter</kbd>|Inserts the highlighted completion, inserts the highlighted directory, or jumps to the highlighted command history entry without executing it.
<kbd>Ctrl</kbd>+<kbd>Enter</kbd>|Same as <kbd>Shift</kbd>+<kbd>Enter</kbd>.

Most of the popup windows also have incremental search:

Key | Description
:-:|---
Typing|Typing does an incremental search.
<kbd>F3</kbd>|Go to the next match.
<kbd>Ctrl</kbd>+<kbd>L</kbd>|Go to the next match.
<kbd>Shift</kbd>+<kbd>F3</kbd>|Go to the previous match.
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd>|Go to the previous match.

The `win-history-list` command has a different search feature.  Typing digits `0`-`9` jumps to the numbered history entry, or typing a letter jumps to the preceding history entry that begins with the typed letter.  This is for compatibility with the <kbd>F7</kbd> behavior built into Windows console prompts.  Use the `clink-popup-history` command instead if you prefer for typing to do an incremental search.

<a name="extending-clink"></a>

# Extending Clink With Lua

Clink can be extended with [Lua](https://www.lua.org/docs.html) scripts to customize startup actions, create completion matches, customize the prompt, and more.  The following sections describe these in more detail and show some examples.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#lua-scripts-location">Location of Lua Scripts</a></td><td class="lmtd">Locations from which scripts are loaded.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#matchgenerators">Match Generators</a></td><td class="lmtd">How to write match generators, or custom completion providers.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#argumentcompletion">Argument Completion</a></td><td class="lmtd">How to give commands contextual match generators for their arguments.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#classifywords">Coloring the Input Text</a></td><td class="lmtd">How to make a match generator or argument matcher override the input coloring.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#customisingtheprompt">Customizing the Prompt</a></td><td class="lmtd">How to write custom prompt filters.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#customisingsuggestions">Customizing Suggestions</a></td><td class="lmtd">How to write custom suggestion generators.</td></tr>
</table>

<a name="lua-scripts-location"></a>

## Location of Lua Scripts

Clink loads all Lua scripts it finds in these directories:
1. All directories listed in the `clink.path` setting, separated by semicolons.
2. If `clink.path` is not set, then the DLL directory and the profile directory are used (see [File Locations](#filelocations) for info about the profile directory).
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
  - Provide new custom commands that can be bound to keys via the [luafunc: key macro syntax](#luakeybindings).
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

<span class="arg">line_state</span> is a [line_state](#line_state) object that has information about the current line.

<span class="arg">match_builder</span> is a [builder](#builder) object to which matches can be added.

If no further match generators need to be called then the function should return true.  Returning false or nil continues letting other match generators get called.

Here is an example script that supplies git branch names as matches for `git checkout`.  This example doesn't handle git aliases, but that could be added with additional script code.

```lua
#INCLUDE [docs\examples\ex_generate.lua]
```

### The :getwordbreakinfo() Function

If needed, a generator can optionally influence word breaking for the end word by defining a `:getwordbreakinfo()` function.

The function takes a <span class="arg">line_state</span> [line_state](#line_state) object that has information about the current line.  If it returns nil or 0, the end word is truncated to 0 length.  This is the normal behavior, which allows Clink to collect and cache all matches and then filter them based on typing.  Or it can return two numbers:  word break length and an optional end word length.  The end word is split at the word break length:  one word contains the first word break length characters from the end word (if 0 length then it's discarded), and the next word contains the rest of the end word truncated to the optional word length (0 if omitted).

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

A match generator or [luafunc: key binding](#luakeybindings) can use [clink.onfiltermatches()](#clink.onfiltermatches) to register a function that will be called after matches are generated but before they are displayed or inserted.

The function receives a table argument containing the matches to be displayed, a string argument indicating the completion type, and a boolean argument indicating whether filename completion is desired. The table argument has a `match` string field and a `type` string field; these are the same as in [builder:addmatch()](#builder:addmatch).

The possible completion types are:

Type | Description | Example
---|---|---
`"?"`  | List the possible completions. | `possible-completions` or `popup-complete`
`"*"`  |Insert all of the possible completions. | `insert-completions`
`"\t"` | Do standard completion. | `complete`
`"!"`  | Do standard completion, and list all possible completions if there is more than one. | `complete` (when the `show-all-if-ambiguous` config variable is set)
`"@"`  | Do standard completion, and list all possible completions if there is more than one and partial completion is not possible. | `complete` (when the `show-all-if-unmodified` config variable is set)
`"%"`  | Do menu completion (cycle through possible completions). | `menu-complete` or `old-menu-complete`

The return value is a table with the input matches filtered as desired. The match filter function can remove matches, but cannot add matches (use a match generator instead).  If only one match remains after filtering, then many commands will insert the match without displaying it.  This makes it possible to spawn a process (such as [fzf](https://github.com/junegunn/fzf)) to perform enhanced completion by interactively filtering the matches and keeping only one selected match.

```lua
#INCLUDE [docs\examples\ex_fzf.lua]
```

<a name="filteringthematchdisplay"></a>

#### Filtering the Match Display

In some instances it may be preferable to display different text when listing potential matches versus when inserting a match in the input line, or to display a description next to a match.  For example, it might be desirable to display a `*` next to some matches, or to show additional information about some matches.

The simplest way to do that is just include the `display` and/or `description` fields when using [builder:addmatch()](#builder:addmatch).  Refer to that function's documentation for usage details.

However, older versions of Clink don't support those fields.  And it may in some rare cases it may be desirable to display a list of possible completions that includes extra matches, or omits some matches (but that's discouraged because it can be confusing to users).

A match generator can alternatively use [clink.ondisplaymatches()](#clink.ondisplaymatches) to register a function that will be called before matches are displayed (this is reset every time match generation is invoked).

The function receives a table argument containing the matches to be displayed, and a boolean argument indicating whether they'll be displayed in a popup window. The table argument has a `match` string field and a `type` string field; these are the same as in [builder:addmatch()](#builder:addmatch). The return value is a table with the input matches filtered as required by the match generator.

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

> **Note:**  In v1.3.1 and higher, the table received by the registered ondisplaymatches function includes all the match fields (such as `display`, `description`, `appendchar`, etc), and the returned table can also include any of these fields.  In other words, in v1.3.1 and higher match filtering supports all the same fields as [builder:addmatch()](#builder:addmatch()).

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

### Descriptions for Flags and Arguments

Flags and arguments may optionally have descriptions associated with them.  The descriptions, if any, are displayed when listing possible completions.

Use [_argmatcher:adddescriptions()](#_argmatcher:adddescriptions) to add descriptions for flags and/or arguments.  Refer to its documentation for further details about how to use it, including how to also show arguments that a flag accepts.

For example, with the following matcher, typing `foo -`<kbd>Alt</kbd>+<kbd>=</kbd> will list all of the flags, plus descriptions for each.

```lua
clink.argmatcher("foo")
:addflags("-?", "-h", "-n", "-v", "--help", "--nothing", "--verbose")
:addarg("print", "delete")
:addarg(clink.filematches)
:nofiles()
:adddescriptions(
    { "-n", "--nothing",    description = "Do nothing; show what would happen without doing it" },
    { "-v", "--verbose",    description = "Verbose output" },
    { "-h", "--help", "-?", description = "Show help text" },
    { "print",              description = "Print the specified file" },
    { "delete",             description = "Delete the specified file" },
)
```

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
#INCLUDE [docs\examples\ex_findstr.lua]
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

The functions are passed four arguments, and should return a table of potential matches.

- `word` is a partial string for the word under the cursor, corresponding to the argument for which matches are being generated:  it is an empty string, or if a filename is being entered then it will be the path portion (e.g. for "dir1\dir2\pre" `word` will be "dir1\dir2\").
- `word_index` is the word index in `line_state`, corresponding to the argument for which matches are being generated.
- `line_state` is a [line_state](#line_state) object that contains the words for the associated command line.
- `match_builder` is a [builder](#builder) object (but for adding matches the function should return them in a table).

> **Compatibility Note:** When a function argument uses the old v0.4.9 `clink.match_display_filter` approach, then the `word` argument will be the full word under the cursor, for compatibility with the v0.4.9 API.

Some built-in matcher functions are available:

Function | Description
:-: | ---
[clink.dirmatches](#clink.dirmatches) | Generates directory matches.
[clink.filematches](#clink.filematches) | Generates file matches.

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
:addarg({ "one", "won" })           -- Arg #1
:addarg({ "two", "too" })           -- Arg #2
:addflags("-a", "-b", "/?", "/h")   -- Flags
```

With the shorthand form flags are implied rather than declared.  When a shorthand table's first value is a string starting with `-` or `/` then the table is interpreted as flags.  Note that it's still possible with shorthand form to mix flag prefixes, and even add additional flag prefixes, such as <code>{ <span class="hljs-string">'-a'</span>, <span class="hljs-string">'/b'</span>, <span class="hljs-string">'=c'</span> }</code>.

<a name="classifywords"></a>

## Coloring the Input Text

When the `clink.colorize_input` [setting](#clinksettings) is enabled, [argmatcher](#argumentcompletion) automatically apply colors to the input text by parsing it.

It's possible for an argmatcher to provide a function to override how its arguments are colored.  This function is called once for each of the argmatcher's arguments.

It's also possible to register a classifier function for the whole input line.  This function is very similar to a match generator; classifier functions are called in priority order, and a classifier can choose to stop the classification process.

### More Advanced Stuff

#### Setting a classifier function in an argmatcher

In cases where an [argmatcher](#argumentcompletion) isn't able to color the input text in the desired manner, it's possible to supply a classifier function that overrides how the argmatcher colors the input text.  An argmatcher's classifier function is called once for each word the argmatcher parses, but it can classify any words (not just the word it was called for).  Each argmatcher can have its own classifier function, so when there are linked argmatchers more than one function may be invoked.

Words are colored by classifying the words, and each classification has an associated color.  See [word_classifications:classifyword()](#word_classifications:classifyword) for the available classification codes.

The `clink set` command has different syntax depending on the setting type, so the argmatcher for `clink` needs help in order to get everything right.  A custom generator function parses the input text to provide appropriate matches, and a custom classifier function applies appropriate coloring.

```lua
#INCLUDE [docs\examples\ex_classify_samp.lua]
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
    -- See further below for how to use the commands argument.
    -- Returning true stops any further classifiers from being called, or
    -- returning false or nil continues letting other classifiers get called.
end
```

<span class="arg">commands</span> is a table of tables, with the following scheme:

```lua
-- commands[n].line_state           [line_state] Contains the words for the Nth command.
-- commands[n].classifications      [word_classifications] Use this to classify the words.
```

The <code>line_state</code> field is a [line_state](#line_state) object that contains the words for the associated command line.

The <code>classifications</code> field is a [word_classifications](#word_classifications) object to use for classifying the words in the associated command line.

```lua
#INCLUDE [docs\examples\ex_classify_envvar.lua]
```

<a name="customisingtheprompt"></a>

## Customizing the Prompt

Before Clink displays the prompt it filters the prompt through [Lua](#extending-clink) so that the prompt can be customized. This happens each and every time that the prompt is shown which allows for context sensitive customizations (such as showing the current branch of a git repository).

Writing a prompt filter is straightforward:
1. Create a new prompt filter by calling [clink.promptfilter()](#clink.promptfilter) along with a priority id which dictates the order in which filters are called. Lower priority ids are called first.
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
#INCLUDE [docs\examples\ex_prompt.lua]
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

The right side prompt can be filtered through [Lua](#extending-clink) just like the normal prompt can be.  Simply define a `:rightfilter()` function on the prompt filter returned by a call to [clink.promptfilter()](#clink.promptfilter).  A prompt filter can define both `:filter()` and `:rightfilter()`, or can define only `:filter()`.

The `:rightfilter()` function works the same as the `:filter()` function, except that it operates on the right side prompt.  It takes a string argument that contains the filtered right side prompt so far.  If the rightfilter function returns nil, it has no effect.  If the rightfilter function returns a string, that string is used as the new filtered right side prompt (and may be further modified by other prompt filters with higher priority ids).  If either the rightfilter function or the normal filter function returns a string and a boolean, then if the boolean is false the prompt filtering is done and no further filter functions are called.

This example modifies the right side prompt by prepending the current date:

```lua
#INCLUDE [docs\examples\ex_right_prompt.lua]
```

<br/>

> **Notes:**
> - If the console font and encoding are mismatched, or if some kinds of emoji are present, then the right side prompt might show up positioned incorrectly.  If that happens, try adjusting the font or encoding (e.g. sometimes running `chcp utf-8` can resolve positioning issues).
> - If the `:filter()` function returns a string and false to stop filtering, then the `:rightfilter()` is not called (because no further filter functions are called).  If you want to stop filtering but have both a left and right side prompt, then return only a string from `:filter()` and return a string and false from `:rightfilter()`.

<a name="asyncpromptfiltering"></a>

#### Asynchronous Prompt Filtering

Prompt filtering needs to be fast, or it can interfere with using the shell (e.g. `git status` can be slow in a large repo).

Clink provides a way for prompt filters to do some initial work and set the prompt, continue doing work in the background, and then refresh the prompt again when the background work is finished.  This is accomplished by using [Lua coroutines](https://www.lua.org/manual/5.2/manual.html#2.6), but Clink simplifies and streamlines the process.

A prompt filter can call [clink.promptcoroutine(my_func)](#clink.promptcoroutine) to run `my_func()` inside a coroutine.  Clink will automatically resume the coroutine repeatedly while input line editing is idle.  When `my_func()` completes, Clink will automatically refresh the prompt by triggering prompt filtering again.

Typically the motivation to use asynchronous prompt filtering is that one or more <code><span class="hljs-built_in">io</span>.<span class="hljs-built_in">popen</span>(<span class="hljs-string">"some slow command"</span>)</code> calls take too long.  They can be replaced with [io.popenyield()](#io.popenyield) calls inside the prompt coroutine to let them run in the background.

> **Global data:** If `my_func()` needs to use any global data, then it's important to use [clink.onbeginedit()](#clink.onbeginedit) to register an event handler that can reset the global data for each new input line session.  Otherwise the data may accidentally "bleed" across different input line sessions.
>
> **Backward compatibility:** A prompt filter must handle backward compatibility itself if it needs to run on versions of Clink that don't support asynchronous prompt filtering (v1.2.9 and lower).  E.g. you can use <code><span class="hljs-keyword">if</span> clink.promptcoroutine <span class="hljs-keyword">then</span></code> to test whether the API exists.

The following example illustrates running `git status` in the background.  It also remembers the status from the previous input line, so that it can reduce flicker by using the color from last time until the background status operation completes.

```lua
#INCLUDE [docs\examples\ex_async_prompt.lua]
```

<a name="transientprompts"></a>

#### Transient Prompt

Clink can replace a past prompt with a differently formatted "transient" prompt.  For example, if your normal prompt contains many bits of information that don't need to be seen later, then it may be desirable to replace past prompts with a simpler prompt.  Or it may be useful to update the timestamp in a prompt to indicate when the prompt was completed, rather than when it was first shown.

The `%CLINK_TRANSIENT_PROMPT%` environment variable provides the initial prompt string for the transient prompt.

Turn on the transient prompt with `clink set prompt.transient always`.  Or use `same_dir` instead of `always` to only use a transient prompt when the current directory is the same as the previous prompt.

The transient prompt can be customized by a prompt filter:
1. Create a new prompt filter by calling [clink.promptfilter()](#clink.promptfilter) along with a priority id which dictates the order in which filters are called. Lower priority ids are called first.
2. Define a `:transientfilter()` function on the returned prompt filter.

The transient filter function takes a string argument that contains the filtered prompt so far.  If the filter function returns nil, it has no effect.  If the filter function returns a string, that string is used as the new filtered prompt (and may be further modified by other prompt filters with higher priority ids).  If the filter function returns a string and a boolean, then if the boolean is false the prompt filtering is done and no further filter functions are called.

A transient right side prompt is also possible (similar to the usual [right side prompt](#rightprompt)).  The `%CLINK_TRANSIENT_RPROMPT%` environment variable (note the `R` in `_RPROMPT`) provides the initial prompt string for the transient right side prompt, which can be customized by a `:transientrightfilter()` function on a prompt filter.

A prompt filter must have a `:filter()` function defined on it, and may in addition have any combination of `:rightfilter()`, `:transientfilter()`, and `:transientrightfilter()` functions defined on it.

The next example shows how to make a prompt that shows:
1. The current directory and ` > ` on the left, and the date and time on the right.
2. Just `> ` on the left, for past commands.

```lua
#INCLUDE [docs\examples\ex_transient_prompt.lua]
```

<a name="customisingsuggestions"></a>

## Customizing Suggestions

Clink can offer suggestions how to complete a command as you type, and you can select how it generates suggestions.

Turn on [automatic suggestions](#autosuggest_enable) with `clink set autosuggest.enable true`.  Once enabled, Clink will show suggestions in a [muted color](#color.suggestion) after the end of the typed command.  Accept the whole suggestion with the <kbd>Right</kbd> arrow or <kbd>End</kbd> key, accept the next word of the suggestion with <kbd>Ctrl</kbd>+<kbd>Right</kbd>, or accept the next full word of the suggestion up to a space with <kbd>Shift</kbd>+<kbd>Right</kbd>.  You can ignore the suggestion if it isn't what you want; suggestions have no effect unless you accept them first.

Scripts can provide custom suggestion generators, in addition to the built-in options:
1. Create a new suggestion generator by calling [clink.suggester()][#clink.suggester] along with a name that identifies the suggestion generator, and can be added to the [`autosuggest.strategy`](#autosuggest_strategy) setting.
2. Define a `:suggest()` function on the returned suggestion generator.

The function takes a [line_state](#line_state) argument that contains the input line, and a [matches](#matches) argument that contains the possible matches from the completion engine.  If the function returns nil, the next generator listed in the strategy is called.  If the function returns a string (even an empty string), then the string is used as the suggestion.

The function can optionally return a string and an offset to where the suggestion begins in the input line.  This makes it easier to return suggestions in some cases, and also makes it possible to update the capitalization of the whole accepted suggestion (even the part that's already been typed).

This example illustrates how to make a suggestion generator that returns the longest common prefix of the possible matches.

```lua
#INCLUDE [docs\examples\ex_suggest.lua]
```

# Miscellaneous

These sections provide more information about various aspects of Clink:

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#keybindings">Customizing Key Bindings</a></td><td class="lmtd">How to customize key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#saved-command-history">Saved Command History</a></td><td class="lmtd">How the saved command history works.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#using-history-expansion">Using History Expansion</a></td><td class="lmtd">How to use history expansion.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#popular-scripts">Popular Scripts</a></td><td class="lmtd">Some popular scripts to enhance Clink.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#troubleshooting-tips">Troubleshooting Tips</a></td><td class="lmtd">How to troubleshoot and report problems.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#privacy">Privacy</a></td><td class="lmtd">Privacy statement for Clink.</td></tr>
</table>

<a name="keybindings"></a>

## Customizing Key Bindings

Key bindings are defined in .inputrc files.

The `clink-show-help` command is bound to <kbd>Alt</kbd>+<kbd>H</kbd> and lists all currently active key bindings.  The list displays "friendly" key names, and these names are generally not suitable for use in .inputrc files.  For example "Up" is the friendly name for `"\e[A"`, and "A-C-F2" is the friendly name for `"\e\e[1;5Q"`.  To see key sequence strings suitable for use in .inputrc files use `clink echo` as described below.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#the-inputrc-file">The .inputrc file</a></td><td class="lmtd">A quick summary of what key binding lines look like.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#sample-inputrc-file">Sample .inputrc file</a></td><td class="lmtd">Some sample key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#discoverkeysequences">Discovering Clink key sequences</a></td><td class="lmtd">How to find key names to use for key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#specialkeys">Binding special keys</a></td><td class="lmtd">A table of special key names.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#luakeybindings">Lua key bindings</a></td><td class="lmtd">How to bind keys to Lua functions.</td></tr>
</table>

### The .inputrc file

You can use `clink info` to find the directories and configuration files for the current Clink session, including where the .inputrc file is located, or can be located.  See the [Readline Init File](#init-file) section for detailed information about .inputrc files.

Here is a quick summary about key binding lines in .inputrc files:

- A key binding is <code><span class="arg">name</span>: <span class="arg">function</span></code> or <code><span class="arg">name</span>: "<span class="arg">literal text</span>"</code>.
- Key names are like this:
  - `C-a` and `"\C-a"` are both <kbd>Ctrl</kbd>+<kbd>a</kbd>.
  - `M-a` and `"\M-a"` are both <kbd>Alt</kbd>+<kbd>a</kbd>.
  - `M-C-a` and `"\M-\C-a"` are both <kbd>Alt</kbd>+<kbd>Ctrl</kbd>+<kbd>a</kbd>.
  - `hello` is just <kbd>h</kbd>; the `ello` is a syntax error and is silently discarded by Readline.
  - `"hello"` is the series of keys <kbd>h</kbd>,<kbd>e</kbd>,<kbd>l</kbd>,<kbd>l</kbd>,<kbd>o</kbd>.
  - Special keys like <kbd>Up</kbd> are represented by VT220 escape codes such as`"\e[A"` (see [Binding special keys](#specialkeys) for more info).
- Key bindings can be either functions or macros (literal text):
  - `blah-blah` binds to a function named "blah-blah".
  - `"blah-blah"` inserts the literal text "blah-blah".

See [Discovering Clink key sequences](#discoverkeysequences) to learn how to find key names for keys that you want to bind.

### Sample .inputrc file

Here is a sample `.inputrc` file with some of the key bindings that I use:

<pre><code class="plaintext"><span class="hljs-meta">$if clink</span>           <span class="hljs-comment"># begin clink-only section</span>

<span class="hljs-comment"># The following key bindings are for emacs mode.</span>
<span class="hljs-meta">set keymap emacs</span>

<span class="hljs-comment"># Completion key bindings.</span>
<span class="hljs-string">"\t"</span>:               old-menu-complete               <span class="hljs-comment"># Tab</span>
<span class="hljs-string">"\e[Z"</span>:             old-menu-complete-backward      <span class="hljs-comment"># Shift+Tab</span>
<span class="hljs-string">"\e[27;5;9~"</span>:       clink-popup-complete            <span class="hljs-comment"># Ctrl+Tab</span>

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

> **Note:** Third party console hosts such as ConEmu may have their own key bindings that supersede Clink.  They usually have documentation for how to change or disable their key bindings to allow console programs to handle the keys instead.

<a name="discoverkeysequences"></a>

### Discovering Clink key sequences

Clink provides an easy way to find the key sequence for any key combination that Clink supports. Run `clink echo` and then press key combinations; the associated key binding sequence is printed to the console output and can be used for a key binding in the inputrc file.

A chord can be formed by concatenating multiple key binding sequences. For example, `"\C-X"` and `"\e[H"` can be concatenated to form `"\C-X\e[H"` representing the chord <kbd>Ctrl</kbd>+<kbd>X</kbd>,<kbd>Home</kbd>.

When finished, press <kbd>Ctrl</kbd>+<kbd>C</kbd> to exit from `clink echo`.

> **Note:** With non-US keyboard layouts, `clink echo` is not able to ignore dead key input (accent keys, for example).  It print the key sequence for the dead key itself, which is not useful.  You can ignore that and press the next key, and then it prints the correct key sequence to use in key bindings.

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

### Lua key bindings

You can bind a key to a [Lua](#extending-clink) function by [binding](#keybindings) it to a macro that begins with "luafunc:".  Clink will invoke the named Lua function when the key binding is input.  Function names can include periods (such as `foo.bar`) but cannot include any other punctuation.

The Lua function receives two arguments:

<span class="arg"><a href="#rl_buffer">rl_buffer</a></span> gives it access to the input buffer.

<span class="arg"><a href="#line_state">line_state</a></span> gives it access to the same line state that a [match generator](#match-generators) receives.

Lua functions can print output, but should first call [rl_buffer:beginoutput()](#rl_buffer:beginoutput) so that the output doesn't overwrite the displayed input line.

> **Notes:**
> - The <span class="arg">line_state</span> is nil if not using Clink v1.2.34 or higher.
> - The end word is always empty for generators.  So to get the word at the cursor use:
> ```lua
> local info = line_state:getwordinfo(line_state:getwordcount())
> local word_at_cursor = line_state:getline():sub(info.offset, line_state:getcursor())
> ```

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
#INCLUDE [docs\examples\ex_findprompt.lua]

#INCLUDE [docs\examples\ex_findline.lua]
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

## Using History Expansion

Clink uses Readline's [History library](https://tiswww.cwru.edu/php/chet/readline/history.html) to
add history expansion capabilities.  If these are undesirable, they can be turned off by running
`clink set history.expand_mode off`.

The History library provides a history expansion feature that is similar
to the history expansion provided by `csh`.  This section describes the
syntax used to manipulate the history information.

   History expansions introduce words from the history list into the
input stream, making it easy to repeat commands, insert the arguments to
a previous command into the current input line, or fix errors in
previous commands quickly.

   History expansion takes place in two parts.  The first is to
determine which line from the history list should be used during
substitution.  The second is to select portions of that line for
inclusion into the current one.  The line selected from the history is
called the "event", and the portions of that line that are acted upon
are called "words".  Various "modifiers" are available to manipulate the
selected words.  The line is broken into words in the same fashion that
Bash does, so that several words surrounded by quotes are considered one
word.  History expansions are introduced by the appearance of the
history expansion character, which is `!`.

   History expansion implements shell-like quoting conventions: a
backslash can be used to remove the special handling for the next
character; single quotes enclose verbatim sequences of characters, and
can be used to inhibit history expansion; and characters enclosed within
double quotes may be subject to history expansion, since backslash can
escape the history expansion character, but single quotes may not, since
they are not treated specially within double quotes.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#event-designators">Event Designators</a></td><td class="lmtd">How to specify which history line to use.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#word-designators">Word Designators</a></td><td class="lmtd">Specifying which words are of interest.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#modifiers">Modifiers</a></td><td class="lmtd">Modifying the results of substitution.</td></tr>
</table>

### Event Designators

An event designator is a reference to a command line entry in the
history list.  Unless the reference is absolute, events are relative to
the current position in the history list.

<table>
<tr><td>
<code>!</code>
</td><td>
     Start a history substitution, except when followed by a space, tab,
     the end of the line, or <code>=</code>.
</td></tr>

<tr><td>
<code>!<em>n</em></code>
</td><td>
     Refer to command line <em>n</em>.
</td></tr>

<tr><td>
<code>!-<em>n</em></code>
</td><td>
     Refer to the command <em>n</em> lines back.
</td></tr>

<tr><td>
<code>!!</code>
</td><td>
     Refer to the previous command.  This is a synonym for <code>!-1</code>.
</td></tr>

<tr><td>
<code>!<em>string</em></code>
</td><td>
     Refer to the most recent command preceding the current position in
     the history list starting with <em>string</em>.
</td></tr>

<tr><td>
<code>!?<em>string</em>[?]</code>
</td><td>
     Refer to the most recent command preceding the current position in
     the history list containing <em>string</em>.  The trailing <code>?</code> may be
     omitted if the <em>string</em> is followed immediately by a newline.  If
     <em>string</em> is missing, the string from the most recent search is used;
     it is an error if there is no previous search string.
</td></tr>

<tr><td>
<code>^<em>string1</em>^<em>string2</em>^</code>
</td><td>
     Quick Substitution.  Repeat the last command, replacing <em>string1</em>
     with <em>string2</em>.  Equivalent to <code>!!:s^<em>string1</em>^<em>string2</em>^</code>.
</td></tr>

<tr><td>
<code>!#</code>
</td><td>
     The entire command line typed so far.
</td></tr>
</table>

### Word Designators

Word designators are used to select desired words from the event.  A `:`
separates the event specification from the word designator.  It may be
omitted if the word designator begins with a `^`, `$`, `*`, `-`, or `%`.
Words are numbered from the beginning of the line, with the first word
being denoted by 0 (zero).  Words are inserted into the current line
separated by single spaces.

   For example,

<table>
<tr><td>
<code>!!</code>
</td><td>
     designates the preceding command.  When you type this, the
     preceding command is repeated in toto.
</td></tr>

<tr><td>
<code>!!:$</code>
</td><td>
     designates the last argument of the preceding command.  This may be
     shortened to <code>!$</code>.
</td></tr>

<tr><td>
<code>!fi:2</code>
</td><td>
     designates the second argument of the most recent command starting
     with the letters <code>fi</code>.
</td></tr>
</table>

   Here are the word designators:

<table>
<tr><td>
<code>0 (zero)</code>
</td><td>
     The 0th word.  For many applications, this is the command word.
</td></tr>

<tr><td>
<code><em>n</em></code>
</td><td>
     The <em>n</em>th word.
</td></tr>

<tr><td>
<code>^</code>
</td><td>
     The first argument; that is, word 1.
</td></tr>

<tr><td>
<code>$</code>
</td><td>
     The last argument.
</td></tr>

<tr><td>
<code>%</code>
</td><td>
     The first word matched by the most recent <code>!?<em>string</em>?</code> search, if the
     search string begins with a character that is part of a word.
</td></tr>

<tr><td>
<code><em>x</em>-<em>y</em></code>
</td><td>
     A range of words; <code>-<em>y</em></code> abbreviates <code>0-<em>y</em></code>.
</td></tr>

<tr><td>
<code>*</code>
</td><td>
     All of the words, except the 0th.  This is a synonym for <code>1-$</code>.
     It is not an error to use <code>*</code> if there is just one word in the
     event; the empty string is returned in that case.
</td></tr>

<tr><td>
<code><em>x</em>*</code>
</td><td>
     Abbreviates <code><em>x</em>-$</code>
</td></tr>

<tr><td>
<code><em>x</em>-</code>
</td><td>
     Abbreviates <code><em>x</em>-$</code> like <code><em>x</em>*</code>, but omits the last word.  If <code><em>x</em></code> is
     missing, it defaults to 0.
</td></tr>
</table>

   If a word designator is supplied without an event specification, the
previous command is used as the event.

### Modifiers

After the optional word designator, you can add a sequence of one or
more of the following modifiers, each preceded by a `:`.  These modify,
or edit, the word or words selected from the history event.

<table>
<tr><td>
<code>h</code>
</td><td>
     Remove a trailing pathname component, leaving only the head.
</td></tr>

<tr><td>
<code>t</code>
</td><td>
     Remove all leading pathname components, leaving the tail.
</td></tr>

<tr><td>
<code>r</code>
</td><td>
     Remove a trailing suffix of the form <code>.<em>suffix</em></code>, leaving the
     basename.
</td></tr>

<tr><td>
<code>e</code>
</td><td>
     Remove all but the trailing suffix.
</td></tr>

<tr><td>
<code>p</code>
</td><td>
     Print the new command but do not execute it.
</td></tr>

<tr><td>
<code>s/<em>old</em>/<em>new</em>/</code>
</td><td>
     Substitute <em>new</em> for the first occurrence of <em>old</em> in the event line.
     Any character may be used as the delimiter in place of <code>/</code>.  The
     delimiter may be quoted in <em>old</em> and <em>new</em> with a single backslash.  If
     <code>&</code> appears in <em>new</em>, it is replaced by <em>old</em>.  A single backslash will
     quote the <code>&</code>.  If <em>old</em> is null, it is set to the last <em>old</em>
     substituted, or, if no previous history substitutions took place,
     the last <em>string</em> in a <code>!?<em>string</em>?</code> search.  If <em>new</em> is is null, each
     matching <em>old</em> is deleted.  The final delimiter is optional if it is
     the last character on the input line.
</td></tr>

<tr><td>
<code>&</code>
</td><td>
     Repeat the previous substitution.
</td></tr>

<tr><td>
<code>g</code></br>
</td><td>
     Cause changes to be applied over the entire event line.  Used in
     conjunction with <code>s</code>, as in <code>gs/<em>old</em>/<em>new</em>/</code>, or with <code>&</code>.
</td></tr>

<tr><td>
<code>a</code></br>
</td><td>
     The same as <code>g</code>.
</td></tr>

<tr><td>
<code>G</code>
</td><td>
     Apply the following <code>s</code> or <code>&</code> modifier once to each word in the
     event.
</td></tr>
</table>

<a name="sample-scripts">

## Popular Scripts

Here are some popular scripts that show off what can be done with Clink.

### clink-completions

The [clink-completions](https://github.com/vladimir-kotikov/clink-completions) collection of scripts has a bunch of argument matchers and completion generators for things like git, mercurial, npm, and more.

### clink-flex-prompt

The [clink-flex-prompt](https://github.com/chrisant996/clink-flex-prompt) script is similar to the zsh powerlevel10k theme.  It gives Clink a very customizable prompt, with many style options.  It's extensible so you can add your own segments.

It also takes advantage of Clink's [asynchronous prompt refresh](#asyncpromptfiltering) to make prompts show up instantly, even in large git repos, for example.

### clink-fzf

The [clink-fzf](https://github.com/chrisant996/clink-fzf) script integrates the popular [fzf](https://github.com/junegunn/fzf) "fuzzy finder" tool with Clink.

### oh-my-posh

The [oh-my-posh](https://github.com/JanDeDobbeleer/oh-my-posh) program can generate fancy prompts. Refer to its [documentation](https://ohmyposh.dev) for how to configure it, and for sample themes.

Integrating oh-my-posh with Clink is easy: just save the following text to an `oh-my-posh.lua` file in your Clink scripts directory (run `clink info` to find that), and make sure the `oh-my-posh.exe` program is in a directory listed in the `%PATH%` environment variable (or edit the script below to provide a fully qualified path to the oh-my-posh.exe program). Replace the config with your own configuration and you're good to go.

```lua
-- oh-my-posh.lua
load(io.popen('oh-my-posh.exe --config="C:/Users/me/jandedobbeleer.omp.json" --init --shell cmd'):read("*a"))()
```

### starship

The [starship](https://github.com/starship/starship) program can also generate fancy prompts. Refer to its [documentation](https://starship.rs) for how to configure it.

Integrating starship with Clink is just as easy: save the following text to a `starship.lua` file in your Clink scripts directory (run `clink info` to find that), and make sure the `starship.exe` program is in a directory listed in the `%PATH%` environment variable (or edit the script below to provide a fully qualified path to the starship.exe program). The config file for starship is located at `C:\Users\<username>\.config\starship.toml`.

```lua
-- starship.lua
load(io.popen('starship.exe init cmd'):read("*a"))()
```

### z.lua

The [z.lua](https://github.com/skywind3000/z.lua) tool is a faster way to navigate directories, and it integrates with Clink.

## Troubleshooting Tips

If something seems to malfunction, here are some things to try that often help track down what's going wrong:

- Check if anti-malware software blocked Clink from injecting.
  - Consider adding an exclusion for Clink.
  - The contents of the `clink.log` file often help in determining whether anti-malware software blocked Clink.
  - If it's indeed being blocked by anti-malware software, report the false positive to the publisher of the anti-malware software so they can confirm and update the detection signatures.  There's nothing Clink can do about it.
- Check `clink info`.  E.g. does the state dir look right, do the script paths look right, do the inputrc files look right?
- Check `clink set`.  E.g. do the settings look right?
- Check the `clink.log` file for clues (its location is reported by `clink info`).

When [reporting an issue](https://github.com/chrisant996/clink/issues), please include the following which saves time by answering in advance the usual questions:

- Please describe what was expected to happen.
- Please describe what actually happened.
- Please include the output from `clink info` and `clink set`.
- Please include the `clink.log` file (the location is reported by `clink info`).

<a name="privacy"></a>

## Privacy

Clink does not collect user data.  Clink writes diagnostic information to its local log file, and does not transmit the log file off the local computer.  For the location of the log file, refer to [File Locations](#filelocations) or run `clink info`.
