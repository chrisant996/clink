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
  - Interactive completion list (<kbd>Ctrl</kbd>-<kbd>Space</kbd>).
  - Incremental history search (<kbd>Ctrl</kbd>-<kbd>R</kbd> and <kbd>Ctrl</kbd>-<kbd>S</kbd>).
  - Powerful completion (<kbd>Tab</kbd>).
  - Undo (<kbd>Ctrl</kbd>-<kbd>Z</kbd>).
  - Environment variable expansion (<kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>E</kbd>).
  - Doskey alias expansion (<kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>F</kbd>).
  - Scroll the screen buffer (<kbd>Alt</kbd>-<kbd>Up</kbd>, etc).
  - <kbd>Shift</kbd>-Arrow keys to select text, typing replaces selected text, etc.
  - (press <kbd>Alt</kbd>-<kbd>H</kbd> for many more...)
- Directory shortcuts;
  - Typing a directory name followed by a path separator is a shortcut for `cd /d` to that directory.
  - Typing `..` or `...` is a shortcut for `cd ..` or `cd ..\..` (each additional `.` adds another `\..`).
  - Typing `-` or `cd -` changes to the previous current working directory.
- Scriptable autosuggest with Lua.
- Scriptable completion with Lua.
- Scriptable key bindings with Lua.
- Colored and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds <kbd>Alt</kbd>-<kbd>H</kbd> to display the current key bindings.

# Usage

There are several ways to start Clink.

1. If you installed the auto-run, just start `cmd.exe`.  Run `clink autorun --help` for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `<install_dir>\clink inject`.

Starting Clink injects it into a `cmd.exe` process, where it intercepts a handful of Windows API functions so that it can replace the prompt and input line editing with its own Readline-powered enhancements.

# Getting Started

You can use Clink right away without configuring anything:

- Searchable [command history](#saved-command-history) will be saved between sessions.
- <kbd>Tab</kbd> and <kbd>Ctrl</kbd>-<kbd>Space</kbd> will do match completion two different ways.
- Press <kbd>Alt</kbd>-<kbd>H</kbd> to see a list of the current key bindings.
- Press <kbd>Alt</kbd>-<kbd>Shift</kbd>-<kbd>/</kbd> followed by another key to see what command is bound to the key.

There are three main ways of customizing Clink to your preferences:  the [Readline init file](#init-file) (the `.inputrc` file), the [Clink settings](#clink-settings) (the `clink set` command), and [Lua](#extending-clink-with-lua) scripts.

## How Completion Works

Clink can offer possible completions for the word at the cursor, and can insert them for you.  Clink can complete file names, directories, environment variables, and commands.  It also allows you to provide custom completion generators using Lua scripts that execute inside Clink (see [Extending Clink With Lua](#extending-clink-with-lua)).

By default, pressing <kbd>Tab</kbd> performs completion the same way that bash does on Unix and Linux.  When you press <kbd>Tab</kbd>, Clink finds matches for how to complete the word at the cursor.  It automatically inserts the longest common prefix shared by the possible completions.  If you press <kbd>Tab</kbd> again, it also lists the possible completions.

If you install Clink with "Use enhanced defaults" or if you set [`clink.default_bindings`](#default_bindings) to use "windows" defaults, then pressing <kbd>Tab</kbd> cycles through the possible completions, replacing the word with the next possible completion each time.

Pressing <kbd>Ctrl</kbd>-<kbd>Space</kbd> shows an interactive list of possible completions.  You can use the arrow keys to choose which completion to insert, and you can type to filter the list.  <kbd>Enter</kbd> inserts the selected completion, or <kbd>Space</kbd> inserts the selected completion and makes sure a space follows it to allow typing a next argument.

The first word of each command line is the command or program to execute, or a file to open.  By default, Clink provides completions for the first word based on all executable programs on the system PATH and the current directory, but not non-executable files.  You can turn off executable completion by running `clink set exec.enable false`, or you can adjust its behavior by changing the various `exec.*` settings (see [Clink Settings](#clink-settings) for more information about them).  

See [Completion Commands](#completion-commands) and [Clink Commands](#clink-commands) for more available completion commands.

## Common Configuration

The following sections describe some ways to begin customizing Clink to your taste.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_enhanceddefaults">Enhanced default settings</a></td><td class="lmtd">Optionally use enhanced default settings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_inputrc">Create a .inputrc file</a></td><td class="lmtd">Create a .inputrc file where config variables and key bindings can be set.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_defaultbindings">Bash vs Windows</a></td><td class="lmtd">Make <kbd>Ctrl</kbd>-<kbd>F</kbd> and <kbd>Ctrl</kbd>-<kbd>M</kbd> work like usual on Windows.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_autosuggest">Auto-suggest</a></td><td class="lmtd">How to enable and use automatic suggestions.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_colors">Colors</a></td><td class="lmtd">Configure the colors.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_keybindings">Key Bindings</a></td><td class="lmtd">Customize your key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_mouseinput">Mouse Input</a></td><td class="lmtd">Optionally enable mouse clicks in the input line, etc.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_startupcmdscript">Startup Cmd Script</a></td><td class="lmtd">Optional automatic <code>clink_start.cmd</code> script.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#gettingstarted_customprompt">Custom Prompt</a></td><td class="lmtd">Customizing the command line prompt.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#upgradefrom049">Upgrading from Clink v0.4.9</a></td><td class="lmtd">Notes on upgrading from a very old version of Clink.</td></tr>
</table>

<a name="gettingstarted_enhanceddefaults"></a>

### Enhanced default settings

Clink can be installed with plain defaults, or it can be installed with enhanced default settings that enable more of Clink's enhancements by default.

If you install Clink with the setup program and "Use enhanced default settings" is checked, then the enhanced defaults are activated, and then in some places where this documentation refers to default settings the stated default may have been overridden.

If you install Clink from the .zip file then enhanced default settings are activated when the `default_settings` and `default_inputrc` files are present in the binaries directory or in the profile directory.  The .zip file comes with the files, but their names have a `_` prefix so that enhanced defaults won't automatically take effect.  You can activate the enhanced default settings by renaming the files to remove the `_` prefix.

Here are some of the enhanced defaults.  Review the `default_settings` and `default_inputrc` files for the full list.
- [Automatic suggestions](#gettingstarted_autosuggest) are enabled.
- Many [color settings](#gettingstarted_colors) have colorful defaults.
- Uses [Windows key bindings](#gettingstarted_defaultbindings) by default.
- The [command history](#saved-command-history)'s default [limit](#history_max_lines) is increased to 25,000 entries.
- Completion expands environment variables (the [`match.expand_envvars`]() setting).
- If no completions are found with a prefix search, then a substring search is used (the [`match.substring`](#match_substring) setting).
- <kbd>Ctrl</kbd>-<kbd>D</kbd> does not exit CMD (the [`cmd.ctrld_exits`](#ctrld_exits) setting).

<a name="gettingstarted_inputrc"></a>

### Create a .inputrc file

First you'll want to create a `.inputrc` file, and a good place is in your Windows user profile directory.

This file is used for some configuration, such as key bindings and colored completions.

Create the file by running this command at a CMD prompt:

```cmd
notepad %userprofile%\.inputrc
```

You may want to copy/paste the following sample text into the file as a starting point, and then press <kbd>Ctrl</kbd>-<kbd>S</kbd> to save the file.

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

See [Init File](#init-file) for more information on Readline init files.

<a name="gettingstarted_defaultbindings"></a>

### Bash vs Windows

The default Clink key bindings are the same as in the "bash" shell for Unix/Linux.  That makes some keys like <kbd>Ctrl</kbd>-<kbd>A</kbd>, <kbd>Ctrl</kbd>-<kbd>F</kbd>, and <kbd>Ctrl</kbd>-<kbd>M</kbd> behave differently than you might be used to in CMD.

To instead use the familiar [Windows default key bindings](#default_bindings) you can run `clink set clink.default_bindings windows`.

Or, if you use the setup program with "Use enhanced default settings" checked then "windows" key bindings are the default, and you can run `clink set clink.default_bindings bash` to use the bash default key bindings.

Clink comes with many default key bindings.  Use <kbd>Alt</kbd>-<kbd>H</kbd> to see all of the active key bindings, or use <kbd>Alt</kbd>-<kbd>Shift</kbd>-<kbd>?</kbd> to check what is bound to a specific key.  See [Key Bindings](#gettingstarted_keybindings) to get started configuring your own key bindings.

Here are the differences between the Windows defaults and the bash defaults:

Key | Windows | Bash
-|-|-
<kbd>Ctrl</kbd>-<kbd>A</kbd> | Select All. | Go to beginning of line.
<kbd>Ctrl</kbd>-<kbd>B</kbd> | (not bound) | Move backward one character.
<kbd>Ctrl</kbd>-<kbd>E</kbd> | Expands doskey aliases, environment variables, and [history expansions](#using-history-expansion) in the current line. | Go to end of line.
<kbd>Ctrl</kbd>-<kbd>F</kbd> | Find text (in plain Conhost console windows). | Move forward one character.
<kbd>Ctrl</kbd>-<kbd>M</kbd> | Mark text (in plain Conhost console windows). | Same as <kbd>Enter</kbd>.
<kbd>Tab</kbd> | Cycle forward through available completions. | Bash style completion.
<kbd>Shift</kbd>-<kbd>Tab</kbd> | Cycle backward through available completions. | (not bound)
<kbd>Right</kbd> | Move forward one character, or at the end of the line insert the next character from the previous input line. | Move forward one character.

<a name="gettingstarted_autosuggest"></a>

### Auto-suggest

Clink can suggest commands as you type, based on command history and completions.

To turn on automatic suggestions, run `clink set autosuggest.enable true`.  When the cursor is at the end of the input line, a suggestion may appear in a muted color.  If the suggestion isn't what you want, just ignore it.  Or accept the whole suggestion with the <kbd>Right</kbd> arrow or <kbd>End</kbd> key, accept the next word of the suggestion with <kbd>Ctrl</kbd>-<kbd>Right</kbd>, or accept the next full word of the suggestion up to a space with <kbd>Shift</kbd>-<kbd>Right</kbd>.

The [`autosuggest.strategy`](#autosuggest_strategy) setting determines how a suggestion is chosen.

Or, if you use the setup program with "Use enhanced default settings" checked then automatic suggestions are enabled by default.

<a name="gettingstarted_colors"></a>

### Colors

Clink has many configurable colors for match completions, input line coloring, popup lists, and more.

If you use the setup program with "Use enhanced default settings" checked then many of the color settings have more colorful default values.

#### For completion

When performing completion (e.g. <kbd>Tab</kbd> or <kbd>Ctrl</kbd>-<kbd>Space</kbd>) Clink can add color to the possible completions.

To turn on colored completions, put a line `set colored-stats on` in the [.inputrc file](#gettingstarted_inputrc) (if you copy/pasted the sample text, it's already there).

See the [Completion Colors](#completion-colors) section for how to configure the colors for match completions.

#### Other colors

Clink adds color to the input line by highlighting arguments, flags, doskey macros, and more.  If you don't want input line colors, you can turn it off by running `clink set clink.colorize_input false`.

There are also colors for popup lists, and some other things.

To configure a color, run <code>clink set <span class="arg">colorname</span> <span class="arg">colorvalue</span></code>.

Match completions make it easy to change Clink settings:  type <code>clink set color.</code> and then use completion (e.g. <kbd>Tab</kbd> or <kbd>Ctrl</kbd>-<kbd>Space</kbd>) to see the available color settings, and to fill in a color value.

Here are some colors you may want to set up right away:

Color | Description | Recommended
---|---|---
`color.executable` | Apply color when the command is an executable file. | `clink set color.executable sgr 38;5;32`
`color.unrecognized` | Apply color when the command is not recognized. | `clink set color.unrecognized sgr 38;5;203`

See the [Clink Settings](#clink-settings) and [Color Settings](#color-settings) sections for more information on Clink settings.

<a name="gettingstarted_keybindings"></a>

### Key Bindings

You can customize your key bindings (keyboard shortcuts) by assigning key bindings in the [.inputrc file](#gettingstarted_inputrc).  See [Customizing Key Bindings](#keybindings) for more information.

Clink comes with many pre-configured key bindings that invoke named commands.  Here are a few that you might find especially handy:

<table>
<tr><td><kbd>Alt</kbd>-<kbd>H</kbd></td><td>This is <code>clink-show-help</code>, which lists the key bindings and commands.</td></tr>
<tr><td><kbd>Tab</kbd></td><td>This is <code>complete</code> or <code>old-menu-complete</code>, depending on the <code><a href="#default_bindings">clink.default_bindings</a></code> Clink setting.  <code>complete</code> swhich performs completion by selecting from an interactive list of possible completions; if there is only one match, the match is inserted immediately.</td></tr>
<tr><td><kbd>Ctrl</kbd>-<kbd>Space</kbd></td><td>This is <code>clink-select-complete</code>, which performs completion by selecting from an interactive list of possible completions; if there is only one match, the match is inserted immediately.</td></tr>
<tr><td><kbd>Alt</kbd>-<kbd>=</kbd></td><td>This is <code>possible-completions</code>, which lists the available completions for the current word in the input line.</td></tr>
<tr><td><kbd>Alt</kbd>-<kbd>.</kbd></td><td>This is <code>yank-last-arg</code>, which inserts the last argument from the previous line.  You can use it repeatedly to cycle backwards through the history, inserting the last argument from each line.  Learn more by reading about <a href="#killing-and-yanking">Killing and Yanking</a>.
<tr><td><kbd>Ctrl</kbd>-<kbd>R</kbd></td><td>This is <code>reverse-search-history</code>, which incrementally searches the history.  Press it, then type, and it does a reverse incremental search while you type.  Press <kbd>Ctrl</kbd>-<kbd>R</kbd> again (and again, etc) to search for other matches of the search text.  Learn more by reading about <a href="#searching-for-commands-in-the-history">Searching for Commands in the History</a>.</td></tr>
<tr><td><kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>D</kbd></td><td>This is <code>remove-history</code>, which deletes the currently selected history line after using any of the history search or navigation commands.</td></tr>
<tr><td><kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>K</kbd></td><td>This is <code>add-history</code>, which adds the current line to the history without executing it, and then clears the input line.</td></tr>
<tr><td><kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>N</kbd></td><td>This is <code>clink-menu-complete-numbers</code>, which grabs numbers with 3 or more digits from the current console screen and cycles through inserting them as completions (binary, octal, decimal, hexadecimal).  Super handy for quickly inserting a commit hash that was printed as output from a preceding command.</td></tr>
<tr><td><kbd>Alt</kbd>-<kbd>0</kbd> to <kbd>Alt</kbd>-<kbd>9</kbd></td><td>These are <code>digit-argument</code>, which let you enter a numeric value used by many commands.  For example <kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>W</kbd> copies the current word to the clipboard, but if you first type <kbd>Alt</kbd>-<kbd>2</kbd> followed by <kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>W</kbd> then it copies the 3rd word to the clipboard (the first word is 0, the second is 1, etc).  Learn more by reading about <a href="#readline-arguments">Readline Arguments</a>.</td></tr>
</table>

For a full list of commands available for key bindings, see [Bindable Commands](#bindable-commands).

<a name="gettingstarted_mouseinput"></a>

### Mouse Input

Clink can optionally respond to mouse input, instead of letting the terminal respond to mouse input (e.g. to select text on the screen).  When mouse input is enabled in Clink, you can click in the input line or in popup lists, and the mouse wheel scrolls popup lists.

Use <code>clink set terminal.mouse_input <span class="arg">mode</span></code> with one of the following modes to control whether Clink responds to mouse input:

Mode | Description
---|---
`off` | Lets the terminal host handle mouse input.
`on` | Lets Clink handle mouse input.
`auto` | Lets Clink handle mouse input in ConEmu and in the default Conhost terminal when Quick Edit mode is unchecked in the console Properties dialog.

Use <code>clink set terminal.mouse_modifier <span class="arg">modifiers</span></code> or <code>set CLINK_MOUSE_MODIFIER=<span class="arg">modifiers</span></code> to control which modifier keys must be held for Clink to respond to mouse input.

These select which modifier keys (<kbd>Alt</kbd>, <kbd>Ctrl</kbd>, <kbd>Shift</kbd>) must be held in order for Clink to respond to mouse input when mouse input is enabled by the `terminal.mouse_input` setting.  <span class="arg">modifiers</span> is a text string that can list one or more modifier keys:  "alt", "ctrl", and "shift".  For example, setting it to "alt shift" causes Clink to only respond to mouse input when both <kbd>Alt</kbd> and <kbd>Shift</kbd> are held (and not <kbd>Ctrl</kbd>).  If the `%CLINK_MOUSE_MODIFIER%` environment variable is set then its value
supersedes the `terminal.mouse_modifier` setting.  In Windows Terminal many modifier keys do special things with mouse clicks, so the modifier key combination that interferes least with built in Windows Terminal behaviors is <kbd>Ctrl</kbd>-<kbd>Alt</kbd>.

When mouse input is enabled in Clink, then mouse input works a little differently:
- You can bypass Clink mouse input and use the normal terminal mouse input by holding a different combination of modifier keys than listed in `terminal.mouse_modifier` or `%CLINK_MOUSE_MODIFIER%`.
- Windows Terminal treats <kbd>Shift</kbd>-<kbd>RightClick</kbd> specially and turns off line ending detection when copying the selected text to the clipboard.  Hold <kbd>Ctrl</kbd> or <kbd>Alt</kbd> when right clicking to do the normal copy with line ending detection.
- In ConEmu, the mouse wheel always scrolls the terminal; Clink cannot use it to scroll popup lists.
- In the default Conhost terminal when Quick Edit mode is turned off then Clink will also respond to mouse input when no modifier keys are held.

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
- Some match generator scripts might need adjustments to become fully compatible with the `autosuggest.enable` setting.
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

Name                         | Default [*](#alternatedefault) | Description
:--:                         | :-:     | -----------
`autosuggest.async`          | True    | When this is <code>true</code> matches are generated asynchronously for suggestions.  This helps to keep typing responsive.
<a name="autosuggest_enable"></a>`autosuggest.enable` | False [*](#alternatedefault) | When this is `true` a suggested command may appear in `color.suggestion` color after the cursor.  If the suggestion isn't what you want, just ignore it.  Or accept the whole suggestion with the <kbd>Right</kbd> arrow or <kbd>End</kbd> key, accept the next word of the suggestion with <kbd>Ctrl</kbd>-<kbd>Right</kbd>, or accept the next full word of the suggestion up to a space with <kbd>Shift</kbd>-<kbd>Right</kbd>.  The `autosuggest.strategy` setting determines how a suggestion is chosen.
`autosuggest.original_case`  | True | When this is enabled (the default), accepting a suggestion uses the original capitalization from the suggestion.
<a name="autosuggest_strategy"></a>`autosuggest.strategy` | `match_prev_cmd history completion` | This determines how suggestions are chosen.  The suggestion generators are tried in the order listed, until one provides a suggestion.  There are three built-in suggestion generators, and scripts can provide new ones.  `history` chooses the most recent matching command from the history.  `completion` chooses the first of the matching completions.  `match_prev_cmd` chooses the most recent matching command whose preceding history entry matches the most recently invoked command, but only when the `history.dupe_mode` setting is `add`.
<a name="clink_autostart"></a>`clink.autostart` | | This command is automatically run when the first CMD prompt is shown after Clink is injected.  If this is blank (the default), then Clink instead looks for `clink_start.cmd` in the binaries directory and profile directory and runs them.  Set it to "nul" to not run any autostart command.
`clink.autoupdate`           | True    | When enabled, Clink periodically checks for updates for the Clink program files (see [Automatic Updates](#automatic-updates)).
`clink.colorize_input`       | True    | Enables context sensitive coloring for the input text (see [Coloring the Input Text](#classifywords)).
<a name="default_bindings"></a>`clink.default_bindings` | `bash` [*](#alternatedefault) | Clink uses bash key bindings when this is set to `bash` (the default).  When this is set to `windows` Clink overrides some of the bash defaults with familiar Windows key bindings for <kbd>Tab</kbd>, <kbd>Ctrl</kbd>-<kbd>A</kbd>, <kbd>Ctrl</kbd>-<kbd>F</kbd>, <kbd>Ctrl</kbd>-<kbd>M</kbd>, and <kbd>Right</kbd>.
`clink.gui_popups`           | False   | When set, Clink uses GUI popup windows instead console text popups.  The `color.popup` settings have no effect on GUI popup windows.
`clink.logo`                 | `full`  | Controls what startup logo to show when Clink is injected.  `full` = show full copyright logo, `short` = show abbreviated version info, `none` = omit the logo.
`clink.paste_crlf`           | `crlf`  | What to do with CR and LF characters on paste. Setting this to `delete` deletes them, `space` replaces them with spaces, `ampersand` replaces them with ampersands, and `crlf` pastes them as-is (executing commands that end with a newline).
<a name="clink_dot_path"></a>`clink.path` | | A list of paths from which to load Lua scripts. Multiple paths can be delimited semicolons.
`clink.promptfilter`         | True    | Enable [prompt filtering](#customising-the-prompt) by Lua scripts.
`clink.update_interval`      | `5`     | The Clink autoupdater will wait this many days between update checks (see [Automatic Updates](#automatic-updates)).
`cmd.admin_title_prefix`     |         | When set, this replaces the "Administrator: " console title prefix.
`cmd.altf4_exits`            | True    | When set, pressing <kbd>Alt</kbd>-<kbd>F4</kbd> exits the cmd.exe process.
`cmd.auto_answer`            | `off`   | Automatically answers cmd.exe's "Terminate batch job (Y/N)?" prompts. `off` = disabled, `answer_yes` = answer Y, `answer_no` = answer N.
<a name="ctrld_exits"></a>`cmd.ctrld_exits` | True | <kbd>Ctrl</kbd>-<kbd>D</kbd> exits the cmd.exe process when it is pressed on an empty line.
`cmd.get_errorlevel`         | True    | When this is enabled, Clink runs a hidden `echo %errorlevel%` command before each interactive input prompt to retrieve the last exit code for use by Lua scripts.  If you experience problems, try turning this off.  This is on by default.
`color.arg`                  |         | The color for arguments in the input line when `clink.colorize_input` is enabled.
`color.arginfo`              | `yellow` [*](#alternatedefault) | Argument info color.  Some argmatchers may show that some flags or arguments accept additional arguments, when listing possible completions.  This color is used for those additional arguments.  (E.g. the "dir" in a "-x dir" listed completion.)
`color.argmatcher`           | [*](#alternatedefault) | The color for the command name in the input line when `clink.colorize_input` is enabled, if the command name has an argmatcher available.
<a name="color_cmd"></a>`color.cmd` | `bold` [*](#alternatedefault) | Used when displaying shell (CMD.EXE) command completions, and in the input line when `clink.colorize_input` is enabled.
`color.cmdredir`             | `bold` [*](#alternatedefault) | The color for redirection symbols (`<`, `>`, `>&`) in the input line when `clink.colorize_input` is enabled.
`color.cmdsep`               | `bold` [*](#alternatedefault) | The color for command separaors (`&`, `|`) in the input line when `clink.colorize_input` is enabled.
`color.comment_row`          | `bright white on cyan` [*](#alternatedefault) | The color for the comment row in the `clink-select-complete` command.  The comment row shows the "and <em>N</em> more matches" or "rows <em>X</em> to <em>Y</em> of <em>Z</em>" messages.
`color.description`          | `bright cyan` [*](#alternatedefault) | Used when displaying descriptions for match completions.
<a name="color_doskey"></a>`color.doskey` | `bright cyan` [*](#alternatedefault) | Used when displaying doskey alias completions, and in the input line when `clink.colorize_input` is enabled.
`color.executable`           | [*](#alternatedefault) | When set, this is the color in the input line for a command word that is recognized as an executable file.
<a name="color_filtered"></a>`color.filtered` | `bold` [*](#alternatedefault) | The default color for filtered completions (see [Filtering the Match Display](#filteringthematchdisplay)).
`color.flag`                 | `default` [*](#alternatedefault) | The color for flags in the input line when `clink.colorize_input` is enabled.
<a name="color_hidden"></a>`color.hidden` | [*](#alternatedefault) | Used when displaying file completions with the "hidden" attribute.
`color.horizscroll`          | [*](#alternatedefault) | The color for the `<` or `>` horizontal scroll indicators when Readline's `horizontal-scroll-mode` variable is set.
`color.input`                | [*](#alternatedefault) | The color for input line text. Note that when `clink.colorize_input` is disabled, the entire input line is displayed using `color.input`.
`color.interact`             | `bold`  | The color for prompts such as a pager's `--More?--` prompt.
`color.message`              | `default` | The color for the message area (e.g. the search prompt message, digit argument prompt message, etc).
`color.popup`                |         | When set, this is used as the color for popup lists and messages.  If no color is set, then the console's popup colors are used (see the Properties dialog box for the console window).
`color.popup_desc`           |         | When set, this is used as the color for description column(s) in popup lists.  If no color is set, then a color is chosen to complement the console's popup colors (see the Properties dialog box for the console window).
`color.prompt`               |         | When set, this is used as the default color for the prompt.  But it's overridden by any colors set by [Customizing The Prompt](#customisingtheprompt).
<a name="color_readonly"></a>`color.readonly` | [*](#alternatedefault) | Used when displaying file completions with the "readonly" attribute.
`color.selected_completion`  | [*](#alternatedefault) | The color for the selected completion with the `clink-select-complete` command.  If no color is set, then bright reverse video is used.
`color.selection`            | [*](#alternatedefault) | The color for selected text in the input line.  If no color is set, then reverse video is used.
<a name="color_suggestion"></a>`color.suggestion` | `bright black` [*](#alternatedefault) | The color for automatic suggestions when `autosuggest.enable` is enabled.
`color.unexpected`           | `default` | The color for unexpected arguments in the input line when `clink.colorize_input` is enabled.
`color.unrecognized`         |  [*](#alternatedefault) | When set, this is the color in the input line for a command word that is not recognized as a command, doskey macro, directory, argmatcher, or executable file.
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
`history.dupe_mode`          | `erase_prev` | If a line is a duplicate of an existing history entry Clink will erase the duplicate when this is set to `erase_prev`. Setting it to `ignore` will not add duplicates to the history, and setting it to `add` will always add lines (except when overridden by `history.sticky_search`).
`history.expand_mode`        | `not_quoted` | The `!` character in an entered line can be interpreted to introduce words from the history. This can be enabled and disable by setting this value to `on` or `off`. Values of `not_squoted`, `not_dquoted`, or `not_quoted` will skip any `!` character quoted in single, double, or both quotes respectively.
`history.ignore_space`       | True    | Ignore lines that begin with whitespace when adding lines in to the history.
<a name="history_max_lines"></a>`history.max_lines` | 10000 [*](#alternatedefault) | The number of history lines to save if `history.save` is enabled (1 to 50000).
`history.save`               | True    | Saves history between sessions. When disabled, history is neither read from nor written to a master history list; history for each session exists only in memory until the session ends.
`history.shared`             | False   | When history is shared, all instances of Clink update the master history list after each command and reload the master history list on each prompt.  When history is not shared, each instance updates the master history list on exit.
`history.sticky_search`      | False   | When enabled, reusing a history line does not add the reused line to the end of the history, and it leaves the history search position on the reused line so next/prev history can continue from there (e.g. replaying commands via <kbd>Up</kbd> several times then <kbd>Enter</kbd>, <kbd>Down</kbd>, <kbd>Enter</kbd>, etc).
`history.time_format`        | <code>%F %T &nbsp</code> | This specifies a time format string for showing timestamps for history items.  For a list of format specifiers see `clink set history.time_format` or [History Timestamps](#history-timestamps).
`history.time_stamp`         | `off`   | The default is `off`.  When this is `save`, timestamps are saved for each history item but are only shown when the `--show-time` flag is used with the `history` command.  When this is `show`, timestamps are saved for each history item, and timestamps are shown in the `history` command unless the `--bare` flag is used.
`lua.break_on_error`         | False   | Breaks into Lua debugger on Lua errors.
`lua.break_on_traceback`     | False   | Breaks into Lua debugger on `traceback()`.
<a name="lua_debug"></a>`lua.debug` | False | Loads a simple embedded command line debugger when enabled. Breakpoints can be added by calling [pause()](#pause).
`lua.path`                   |         | Value to append to `package.path`. Used to search for Lua scripts specified in `require()` statements.
<a name="lua_reload_scripts"></a>`lua.reload_scripts` | False | When false, Lua scripts are loaded once and are only reloaded if forced (see [The Location of Lua Scripts](#lua-scripts-location) for details).  When true, Lua scripts are loaded each time the edit prompt is activated.
`lua.strict`                 | True    | When enabled, argument errors cause Lua scripts to fail.  This may expose bugs in some older scripts, causing them to fail where they used to succeed. In that case you can try turning this off, but please alert the script owner about the issue so they can fix the script.
`lua.traceback_on_error`     | False   | Prints stack trace on Lua errors.
<a name="match_expand_envvars"></a>`match.expand_envvars` | False [*](#alternatedefault) | Expands environment variables in a word before performing completion.
`match.fit_columns`          | True    | When displaying match completions, this calculates column widths to fit as many as possible on the screen.
`match.ignore_accent`        | True    | Controls accent sensitivity when completing matches. For example, `ä` and `a` are considered equivalent with this enabled.
`match.ignore_case`          | `relaxed` | Controls case sensitivity when completing matches. `off` = case sensitive, `on` = case insensitive, `relaxed` = case insensitive plus `-` and `_` are considered equal.
`match.limit_fitted_columns` | `0`     | When the `match.fit_columns` setting is enabled, this disables calculating column widths when the number of matches exceeds this value.  The default is 0 (unlimited).  Depending on the screen width and CPU speed, setting a limit may avoid delays.
`match.max_rows`             | `0`     | The maximum number of rows that `clink-select-complete` can use.  When this is 0, the limit is the terminal height.
`match.preview_rows`         | `0`     | The number of rows to show as a preview when using the `clink-select-complete` command (bound by default to <kbd>Ctrl</kbd>-<kbd>Space</kbd>).  When this is 0, all rows are shown and if there are too many matches it instead prompts first like the `complete` command does.  Otherwise it shows the specified number of rows as a preview without prompting, and it expands to show the full set of matches when the selection is moved past the preview rows.
`match.sort_dirs`            | `with`  | How to sort matching directory names. `before` = before files, `with` = with files, `after` = after files.
<a name="match_substring"></a>`match.substring` | False [*](#alternatedefault) | When set, if no completions are found with a prefix search, then a substring search is used.
`match.translate_slashes`    | `system` | File and directory completions can be translated to use consistent slashes.  The default is `system` to use the appropriate path separator for the OS host (backslashes on Windows).  Use `slash` to use forward slashes, or `backslash` to use backslashes.  Use `off` to turn off translating slashes from custom match generators.
`match.wild`                 | True    | Matches `?` and `*` wildcards and leading `.` when using any of the completion commands.  Turn this off to behave how bash does, and not match wildcards or leading dots (but `glob-complete-word` always matches wildcards).
`prompt.async`               | True    | Enables [asynchronous prompt refresh](#asyncpromptfiltering).  Turn this off if prompt filter refreshes are annoying or cause problems.
<a name="prompt-transient"></a>`prompt.transient` | `off` | Controls when past prompts are collapsed ([transient prompts](#transientprompts)).  `off` = never collapse past prompts, `always` = always collapse past prompts, `same_dir` = only collapse past prompts when the current working directory hasn't changed since the last prompt.
`readline.hide_stderr`       | False   | Suppresses stderr from the Readline library.  Enable this if Readline error messages are getting in the way.
`terminal.adjust_cursor_style`| True   | When enabled, Clink adjusts the cursor shape and visibility to show Insert Mode, produce the visible bell effect, avoid disorienting cursor flicker, and to support ANSI escape codes that adjust the cursor shape and visibility. But it interferes with the Windows 10 Cursor Shape console setting. You can make the Cursor Shape setting work by disabling this Clink setting (and the features this provides).
`terminal.differentiate_keys`| False   | When enabled, pressing <kbd>Ctrl</kbd>-<kbd>H</kbd> or <kbd>I</kbd> or <kbd>M</kbd> or <kbd>[</kbd> generate special key sequences to enable binding them separately from <kbd>Backspace</kbd> or <kbd>Tab</kbd> or <kbd>Enter</kbd> or <kbd>Esc</kbd>.
`terminal.east_asian_ambiguous`|`auto` | There is a group of East Asian characters whose widths are ambiguous in the Unicode standard.  This setting controls how to resolve the ambiguous widths.  By default this is set to `auto`, but some terminal hosts may require setting this to a different value to work around limitations in the terminal hosts.  Setting this to `font` measures the East Asian Ambiguous character widths using the current font.  Setting it to `one` uses 1 as the width, or `two` uses 2 as the width.  When this is `auto` (the default) and the current code page is 932, 936, 949, or 950 then the current font is used to measure the widths, or for any other code pages (including UTF8) the East Asian Ambiguous character widths are assumed to be 1.
`terminal.emulation`         | `auto`  | Clink can either emulate a virtual terminal and handle ANSI escape codes itself, or let the console host natively handle ANSI escape codes. `native` = pass output directly to the console host process, `emulate` = clink handles ANSI escape codes itself, `auto` = emulate except when running in ConEmu, Windows Terminal, or Windows 10 new console.
`terminal.mouse_input`       | `auto`  | Clink can optionally respond to mouse input, instead of letting the terminal respond to mouse input (e.g. to select text on the screen).  When mouse input is enabled in Clink, clicking in the input line sets the cursor position, and clicking in popup lists selects an item, etc.  Setting this to `off` lets the terminal host handle mouse input, `on` lets Clink handle mouse input, and `auto` lets Clink handle mouse input in ConEmu and in the default Conhost terminal when Quick Edit mode is unchecked in the console Properties dialog.  For more information see [Mouse Input](#gettingstarted_mouseinput).
`terminal.mouse_modifier`    |       | This selects which modifier keys (<kbd>Alt</kbd>, <kbd>Ctrl</kbd>, <kbd>Shift</kbd>) must be held in order for Clink to respond to mouse input when mouse input is enabled by the `terminal.mouse_input` setting.  This is a text string that can list one or more modifier keys:  'alt', 'ctrl', and 'shift'.  For example, setting it to "alt shift" causes Clink to only respond to mouse input when both <kbd>Alt</kbd> and <kbd>Shift</kbd> are held (and not <kbd>Ctrl</kbd>).  If the `%CLINK_MOUSE_MODIFIER%` environment variable is set then its value supersedes this setting.  For more information see [Mouse Input](#gettingstarted_mouseinput).
<a name="terminal_raw_esc"></a>`terminal.raw_esc` | False | When enabled, pressing <kbd>Esc</kbd> sends a literal escape character like in Unix or Linux terminals.  This setting is disabled by default to provide a more predictable, reliable, and configurable input experience on Windows.  Changing this only affects future Clink sessions, not the current session.
`terminal.use_altgr_substitute`| False | Support Windows' <kbd>Ctrl</kbd>-<kbd>Alt</kbd> substitute for <kbd>AltGr</kbd>. Turning this off may resolve collisions with Readline's key bindings.

<p/>

<a name="alternatedefault"></a>

**&ast;** Some settings have alternative default values when Clink is installed with "Use enhanced default settings" checked in the setup program.  This enables more of Clink's enhancements by default.

> **Compatibility Notes:**
> - The `esc_clears_line` setting has been replaced by a `clink-reset-line` command that is by default bound to the <kbd>Escape</kbd> key.  See [Customizing Key Bindings](#keybindings) for more information.
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

Settings and history are persisted to disk from session to session. By default Clink uses the current user's non-roaming application data directory. This user directory is usually found in one of the following locations;

- Windows XP: `c:\Documents and Settings\<username>\Local Settings\Application Data\clink`
- Windows Vista onwards: `c:\Users\<username>\AppData\Local\clink`

All of the above locations can be overridden using the <code>--profile <span class="arg">path</span></code> command line option which is specified when injecting Clink into cmd.exe using `clink inject`.  Or with the `%CLINK_PROFILE%` environment variable if it is already present when Clink is injected (this envvar takes precedence over any other mechanism of specifying a profile directory, if more than one was used).

You can use `clink info` to find the directories and configuration files for the current Clink session.

### Files

<p>
<dt>.inputrc</dt>
<dd>
This configures the Readline library used by Clink; it can contain key bindings and various settings.  See <a href="#init-file">Readline Init File</a> for details, or see <a href="#gettingstarted_inputrc">Create a .inputrc file</a> for help getting started.
</p>

<p>
<dt>clink_settings</dt>
<dd>
This is where Clink stores its settings.  See <a href="#clink-settings">Clink Settings</a> for more information.

The location of the `clink_settings` file may also be overridden with the `%CLINK_SETTINGS%` environment variable.  This is not recommended because it can be confusing; if the environment variable gets cleared or isn't always set then a different settings file may get used sometimes.  But, one reason to use it is to make your settings sync with other computers.

- `set CLINK_SETTINGS=%USERPROFILE%\OneDrive\clink` can let settings sync between computers through your OneDrive account.
- `set CLINK_SETTINGS=%USERPROFILE%\AppData\Roaming` can let settings sync between computers in a work environment.
</dd></p>

<p>
<dt>clink_history</dt>
<dd>
This is where Clink stores command history.  See <a href="#saved-command-history">Saved Command History</a> for more information.
</dd></p>

<p>
<dt>clink.log</dt>
<dd>
The log file is written in the profile directory.  Clink writes diagnostic information to the log file while Clink is running.  Use `clink info` to find where it is located.
</dd></p>

<p>
<dt>default_settings</dt>
<dd>
This is an optional file.  When Clink loads its settings, it first tries to load default values for settings from a `default_settings` file in either the profile directory or the binaries directory.  Then it loads the `clink_settings` file from the profile directory.

The `default_settings` file can be useful for portable installations or when sharing your favorite Clink configuration with friends.
</dd></p>

<p>
<dt>default_inputrc</dt>
<dd>
This is an optional file.  When Clink loads the <a href="#init-file">Readline Init File</a>, it first tries to load default values from a `default_inputrc` file in either the profile directory or the binaries directory.  Then it loads the `.inputrc` file.

The `default_inputrc` file can be useful for portable installations or when sharing your favorite Clink configuration with friends.
</dd></p>

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
<a name="clinksetcommand"></a>
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
Echos key sequences to use in the .inputrc files for binding keys to Clink commands.  Each key pressed prints the associated key sequence on a separate line, until <kbd>Ctrl</kbd>-<kbd>C</kbd> is pressed.</dd>
</p>

<p>
<dt>clink update</dt>
<dd>
Checks for an updated version of Clink.  If one is available, it is downloaded and will be installed the next time Clink is injected.</dd>
</p>

## Automatic Updates

By default, Clink periodically and automatically checks for new versions.  When an update is available, Clink prints a message on startup.  To apply an update, run `clink update` when convenient to do so.

You can control the frequency of update checks with <code>clink set clink.update_interval <span class="arg">days</span></code>, where <span class="arg">days</span> is the minimum number of days between checking for updates.

You can turn update checks off with `clink set clink.autoupdate false`, or turn them on with `clink set clink.autoupdate true`.

Notes:
- The auto-updater settings are stored in the profile, so different profiles can be configured differently for automatic updates.
- The updater does nothing if the Clink program files are readonly.
- The updater requires PowerShell, which is present by default in Windows 7 and higher.

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

Clink uses the [GNU Readline library](https://tiswww.case.edu/php/chet/readline/rltop.html) to provide line editing functionality, which can be configured to add custom keybindings and macros by creating a Readline init file. The Clink documentation includes an updated and tailored copy of the Readline documentation, below.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#the-basics">The Basics</a></td><td class="lmtd">The basics of using the Readline input editor in Clink.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#init-file">Init File</a></td><td class="lmtd">About the .inputrc init file, configuration variables, and key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#bindable-commands">Bindable Commands</a></td><td class="lmtd">The commands available for key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#completion-colors">Completion Colors</a></td><td class="lmtd">How to customize the completion colors.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#popupwindow">Popup Windows</a></td><td class="lmtd">Using the popup windows.</td></tr>
</table>

## The Basics

### Bare Essentials

To enter characters into the line, simply type them. The typed character appears where the cursor was, and then the cursor moves one space to the right. If you mistype a character, you can use <kbd>Backspace</kbd> to back up and delete the mistyped character.

Sometimes you may mistype a character, and not notice the error until you have typed several other characters. In that case, you can type <kbd>Left</kbd> (the left arrow key) to move the cursor to the left, and then correct your mistake. Afterwards, you can move the cursor to the right with <kbd>Right</kbd> (the right arrow key).

When you add text in the middle of a line, you will notice that characters to the right of the cursor are "pushed over" to make room for the text that you have inserted. Likewise, when you delete text behind the cursor, characters to the right of the cursor are "pulled back" to fill in the blank space created by the removal of the text. A list of the bare essentials for editing the text of an input line follows.

Key | Description
-|-
<kbd>Left</kbd> | Move back one character.
<kbd>Right</kbd> | Move forward one character.
<kbd>Backspace</kbd> | Delete the character to the left of the cursor.
<kbd>Del</kbd> | Delete the character underneath the cursor.
<kbd>A</kbd>, <kbd>a</kbd>, <kbd>1</kbd>, <kbd>!</kbd>, <kbd>"</kbd>, <kbd>/</kbd>, etc | Insert the typed character into the line at the cursor.
<kbd>Ctrl</kbd>-<kbd>z</kbd> or<br/><kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>u</kbd> | Undo the last editing command. You can undo all the way back to an empty line.
<kbd>Home</kbd> | Move to the start of the line.
<kbd>End</kbd> | Move to the end of the line.
<kbd>Ctrl</kbd>-<kbd>Left</kbd> | Move backward a word, where a word is composed of letters and digits.
<kbd>Ctrl</kbd>-<kbd>Right</kbd> | Move forward a word.

### Killing and Yanking

_Killing_ text means to delete the text from the line, but to save it away for later use, usually by _yanking_ (re-inserting) it back into the line. ("Cut" and "paste" are more recent jargon for "kill" and "yank", but killing and yanking do not affect the system's clipboard.)

If the description for a command says that it "kills" text, then you can be sure that you can get the text back in a different (or the same) place later.

When you use a kill command, the text is saved in a _kill-ring_. Any number of consecutive kills save all of the killed text together, so that when you yank it back, you get it all. The kill ring is not line specific; the text that you killed on a previously typed line is available to be yanked back later, when you are typing another line.

Here are some basic commands for killing text.

Key | Description
-|-
<kbd>Ctrl</kbd>-<kbd>End</kbd> | Kill the text from the current cursor position to the end of the line.
<kbd>Ctrl</kbd>-<kbd>Del</kbd> | Kill from the cursor to the end of the current word, or, if between words, to the end of the next word. Word boundaries are the same as those used by <kbd>Ctrl</kbd>-<kbd>Right</kbd>.
<kbd>Ctrl</kbd>-<kbd>Backspace</kbd> | Kill from the cursor the start of the current word, or, if between words, to the start of the previous word. Word boundaries are the same as those used by <kbd>Ctrl</kbd>-<kbd>Left</kbd>.
<kbd>Ctrl</kbd>-<kbd>w</kbd> | Kill from the cursor to the previous whitespace. This is different than <kbd>Ctrl</kbd>-<kbd>Backspace</kbd> because the word boundaries differ.

Here is how to _yank_ the text back into the line. Yanking means to copy the most-recently-killed text from the kill buffer.

Key | Description
-|-
<kbd>Ctrl</kbd>-<kbd>y</kbd> | Yank the most recently killed text back into the buffer at the cursor.
<kbd>Alt</kbd>-<kbd>y</kbd> | Rotate the kill-ring, and yank the new top. You can only do this if the prior command is <kbd>Ctrl</kbd>-<kbd>y</kbd> or <kbd>Alt</kbd>-<kbd>y</kbd>.

### Readline Arguments

You can pass numeric arguments to Readline commands. Sometimes the argument acts as a repeat count, other times it is the _sign_ of the argument that is significant. If you pass a negative argument to a command which normally acts in a forward direction, that command will act in a backward direction. For example, to kill text back to the start of the line, you might type <kbd>Alt</kbd>-<kbd>-</kbd> <kbd>Ctrl</kbd>-<kbd>k</kbd>.

The general way to pass numeric arguments to a command is to type meta digits before the command. If the first "digit" typed is a minus sign (`-`), then the sign of the argument will be negative. Once you have typed one meta digit to get the argument started, you can type the remainder of the digits, and then the command. For example, to give the <kbd>Del</kbd> command an argument of 10, you could type <kbd>Alt</kbd>-<kbd>1</kbd> <kbd>0</kbd> <kbd>Del</kbd>, which will delete the next ten characters on the input line.

<a name="rlsearchinhistory"></a>

### Searching for Commands in the History

Readline provides commands for searching through the command history for lines containing a specified string. There are two search modes: _incremental_ and _non-incremental_.

Incremental searches begin before the user has finished typing the search string. As each character of the search string is typed, Readline displays the next entry from the history matching the string typed so far. An incremental search requires only as many characters as needed to find the desired history entry. To search backward in the history for a particular string, type <kbd>Ctrl</kbd>-<kbd>r</kbd>. Typing <kbd>Ctrl</kbd>-<kbd>s</kbd> searches forward through the history. The characters present in the value of the `isearch-terminators` variable are used to terminate an incremental search. If that variable has not been assigned a value, then <kbd>Esc</kbd> or <kbd>Ctrl</kbd>-<kbd>j</kbd> will terminate an incremental search. <kbd>Ctrl</kbd>-<kbd>g</kbd> will abort an incremental search and restore the original line. When the search is terminated, the history entry containing the search string becomes the current line.

To find other matching entries in the history list, type <kbd>Ctrl</kbd>-<kbd>r</kbd> or <kbd>Ctrl</kbd>-<kbd>s</kbd> as appropriate. This will search backward or forward in the history for the next entry matching the search string typed so far. Any other key sequence bound to a Readline command will terminate the search and execute that command. For instance, <kbd>Enter</kbd> will terminate the search and accept the line, thereby executing the command from the history list. A movement command will terminate the search, make the last line found the current line, and begin editing.

Readline remembers the last incremental search string. If two <kbd>Ctrl</kbd>-<kbd>r</kbd>'s are typed without any intervening characters defining a new search string, any remembered search string is used.

Non-incremental searches read the entire search string before starting to search for matching history lines. The search string may be typed by the user or be part of the contents of the current line. Type <kbd>Alt</kbd>-<kbd>p</kbd> or <kbd>Alt</kbd>-<kbd>n</kbd> to start a non-incremental search backwards or forwards.

To search backward in the history for a line starting with the text before the cursor, type <kbd>PgUp</kbd>. Or search forward by typing <kbd>PgDn</kbd>.

See [Saved Command History](#saved-command-history) for more information on how history works.

## Init File

You can customize key bindings and configuration variables by using an init file.

"[Configuration variables](#readline-configuration-variables)" are customized in the init file, but "[Clink settings](#clink-settings)" are customized with the `clink set` command.

### Init File Location

The Readline init file is named `.inputrc` or `_inputrc`.  Clink searches the directories referenced by the following environment variables in the order listed here, and loads the first `.inputrc` or `_inputrc` file it finds:
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

### Init File Syntax

There are only a few basic constructs allowed in the Readline init file:

- Blank lines are ignored.
- Lines beginning with a `#` are comments.
- Lines beginning with a `$` indicate [conditional init constructs](#conditional-init-constructs).
- Other lines denote [Readline configuration variables](#readline-configuration-variables) and [Readline key bindings](#readline-key-bindings).

#### Readline configuration variables

You can modify the behavior of Readline by altering the values of configuration variables in Readline using the `set` command within the init file. The syntax is simple:

<pre><code class="plaintext">set <span class="arg">variable</span> <span class="arg">value</span></code></pre>

Here, for example, is how to change from the default Emacs-like key binding to use `vi` line editing commands:

<pre><code class="plaintext">set editing-mode vi</code></pre>

Variable names and values, where appropriate, are recognized without regard to case. Unrecognized variable names are ignored.

Boolean variables (those that can be set to on or off) are set to on if the value is null or empty, `on` (case-insensitive), or `1`. Any other value results in the variable being set to off.

Variable | Description
-|-
<a name="configbellstyle"></a>`bell-style` | Controls what happens when Readline wants to ring the terminal bell. If set to "none", Readline never rings the bell. If set to "visible" (the default in Clink), Readline uses a visible bell if one is available. If set to "audible", Readline attempts to ring the terminal's bell.
`blink-matching-paren` | If set to "on", Readline attempts to briefly move the cursor to an opening parenthesis when a closing parenthesis is inserted. The default is "off".
`colored-completion-prefix` | If set to "on", when listing completions, Readline displays the common prefix of the set of possible completions using a different color. The color definitions are taken from the value of the `%LS_COLORS%` environment variable. The default is "off".
`colored-stats` | If set to "on", Readline displays possible completions using different colors to indicate their file type. The color definitions are taken from the value of the `%LS_COLORS%` environment variable. The default is "off".
<a name="configcommentbegin"></a>`comment-begin` | The string to insert at the beginning of the line when the `insert-comment` command is executed. The default value is "::".
<a name="configcompletiondisplaywidth"></a>`completion-display-width` | The number of screen columns used to display possible matches when performing completion. The value is ignored if it is less than 0 or greater than the terminal screen width. A value of 0 will cause matches to be displayed one per line. The default value is -1.
`completion-ignore-case` | If set to "on", Readline performs filename matching and completion in a case-insensitive fashion. The default value is "on".
`completion-prefix-display-length` | The length in characters of the common prefix of a list of possible completions that is displayed without modification. When set to a value greater than zero, common prefixes longer than this value are replaced with an ellipsis when displaying possible completions.
<a namme="configcompletionqueryitems"></a>`completion-query-items` | The number of possible completions that determines when the user is asked whether the list of possibilities should be displayed. If the number of possible completions is greater than or equal to this value, Readline will ask whether or not the user wishes to view them; otherwise, they are simply listed. This variable must be set to an integer value greater than or equal to 0. A negative value means Readline should never ask. The default limit is 100.
`echo-control-characters` | When set to "on", on operating systems that indicate they support it, readline echoes a character corresponding to a signal generated from the keyboard. The default is "on".
`editing-mode` | This controls which Readline input mode is used by default. When set to "emacs" (the default), Readline starts up in Emacs editing mode, where keystrokes are most similar to Emacs. When set to "vi", then `vi` input mode is used.
`emacs-mode-string` | If the `show-mode-in-prompt` variable is enabled, this string is displayed immediately before the last line of the primary prompt when emacs editing mode is active. The value is expanded like a key binding, so the standard set of meta- and control prefixes and backslash escape sequences is available. Use the "\1" and "\2" escapes to begin and end sequences of non-printing characters, which can be used to embed a terminal control sequence into the mode string. The default is "@".
`enable-bracketed-paste` | When set to "on", Readline will configure the terminal in a way that will enable it to insert each paste into the editing buffer as a single string of characters, instead of treating each character as if it had been read from the keyboard. This can prevent pasted characters from being interpreted as editing commands. The default is "on".
`expand-tilde` | If set to "on", tilde expansion is performed when Readline attempts word completion. The default is "off".
`history-preserve-point` | If set to "on", the history code attempts to place the point (the current cursor position) at the same location on each history line retrieved with `previous-history` or `next-history`. The default is "off".
`horizontal-scroll-mode` | This variable can be set to either "on" or "off". Setting it to "on" means that the text of the lines being edited will scroll horizontally on a single screen line when they are longer than the width of the screen, instead of wrapping onto a new screen line. This variable is automatically set to "on" for terminals of height 1. By default, this variable is set to "off".
`isearch-terminators` | The string of characters that should terminate an incremental search without subsequently executing the character as a command (see [Searching for Commands in the History](#rlsearchinhistory)). If this variable has not been given a value, the characters <kbd>Esc</kbd> and <kbd>Ctrl</kbd>-<kbd>j</kbd> will terminate an incremental search.
`keymap` | Sets Readline's idea of the current keymap for key binding commands. Built-in keymap names are `emacs`, `emacs-standard`, `emacs-meta`, `emacs-ctlx`, `vi`, `vi-move`, `vi-command`, and `vi-insert`. `vi` is equivalent to `vi-command` (`vi-move` is also a synonym); `emacs` is equivalent to `emacs-standard`. The default value is `emacs`. The value of the `editing-mode` variable also affects the default keymap.
`mark-directories` | If set to "on", completed directory names have a slash appended. The default is "on".
`mark-modified-lines` | This variable, when set to "on", causes Readline to display an asterisk (`*`) at the start of history lines which have been modified. This variable is "off" by default.
`mark-symlinked-directories` | If set to "on", completed names which are symbolic links to directories have a slash appended (subject to the value of `mark-directories`). The default is "off".
`match-hidden-files` | This variable, when set to "on", causes Readline to match files whose names begin with a `.` (hidden files on Unix and Linux) when performing filename completion. If set to "off", the leading `.` must be supplied by the user in the filename to be completed. This variable is "on" by default.
`menu-complete-display-prefix` | If set to "on", menu completion displays the common prefix of the list of possible completions (which may be empty) before cycling through the list. The default is "off".
`page-completions` | If set to "on", Readline uses an internal `more`-like pager to display a screenful of possible completions at a time. This variable is "on" by default.
`print-completions-horizontally` | If set to "on", Readline will display completions with matches sorted horizontally in alphabetical order, rather than down the screen. The default is "off".
`show-all-if-ambiguous` | This alters the default behavior of the completion functions. If set to "on", words which have more than one possible completion cause the matches to be listed immediately instead of ringing the bell. The default value is "off".
`show-all-if-unmodified` | This alters the default behavior of the completion functions in a fashion similar to `show-all-if-ambiguous`. If set to "on", words which have more than one possible completion without any possible partial completion (the possible completions don't share a common prefix) cause the matches to be listed immediately instead of ringing the bell. The default value is "off".
`show-mode-in-prompt` | If set to "on", add a string to the beginning of the prompt indicating the editing mode: emacs, vi command, or vi insertion. The mode strings are user-settable (e.g., `emacs-mode-string`). The default value is "off".
`skip-completed-text` | If set to "on", this alters the default completion behavior when inserting a single match into the line. It's only active when performing completion in the middle of a word. If enabled, Readline does not insert characters from the completion that match characters after point (the cursor position) in the word being completed, so portions of the word following the cursor are not duplicated. For instance, if this is enabled, attempting completion when the cursor is after the `e` in "Makefile" will result in "Makefile" rather than "Makefilefile", assuming there is a single possible completion. The default value is "off".
`vi-cmd-mode-string` | If the `show-mode-in-prompt` variable is enabled, this string is displayed immediately before the last line of the primary prompt when vi editing mode is active and in command mode. The value is expanded like a key binding, so the standard set of meta- and control prefixes and backslash escape sequences is available. Use the "\1" and "\2" escapes to begin and end sequences of non-printing characters, which can be used to embed a terminal control sequence into the mode string. The default is "(cmd)".
`vi-ins-mode-string` | If the `show-mode-in-prompt` variable is enabled, this string is displayed immediately before the last line of the primary prompt when vi editing mode is active and in insertion mode. The value is expanded like a key binding, so the standard set of meta- and control prefixes and backslash escape sequences is available. Use the "\1" and "\2" escapes to begin and end sequences of non-printing characters, which can be used to embed a terminal control sequence into the mode string. The default is "(ins)".
`visible-stats` | If set to "on", a character denoting a file's type is appended to the filename when listing possible completions. The default is "off".

Clink adds some new configuration variables for Readline:

Variable | Description
-|-
`completion-auto-query-items` | If set to "on", automatically prompts before displaying completions if they won't fit without scrolling (this overrules the [`completion-query-items`](#configcompletionqueryitems) variable). The default is "on".
`history-point-at-end-of-anchored-search` | If set to "on", this puts the cursor at the end of the line when using `history-search-forward` or `history-search-backward`. The default is "off".
`menu-complete-wraparound` | If this is "on", the `menu-complete` family of commands wraps around when reaching the end of the possible completions. The default is "on".
`search-ignore-case` | Controls whether the history search commands ignore case. The default is "on".

Some configuration variables are deprecated in Clink:

Variable | Description
-|-
`bind-tty-special-chars` | Clink doesn't need or use this.
`completion-map-case` | Instead, use the [`match.ignore_case`](#match_ignore_case) Clink setting (see the `relaxed` mode).
`convert-meta` | Clink requires this to be "on", and sets it to "on".
`disable-completion` | If set to "on", Readline will inhibit word completion. Completion characters will be inserted into the line as if they had been mapped to `self-insert`. The default is "off".
`enable-meta-key` | Clink requires this to be "on", and sets it to "on".
`history-size` | Instead, use the [`history.max_lines`](#history_max_lines) Clink setting.
`input-meta` | Clink requires this to be "on", and sets it to "on".
`keyseq-timeout` | Clink does not support this.
`output-meta` | Clink requires this to be "on", and sets it to "on".
`revert-all-at-newline` | Clink always reverts all in-memory changes to history lines each time a new input prompt starts.

#### Readline key bindings

The syntax for controlling key bindings in the init file is simple. First you need to find the name of the command that you want to change. The following sections contain tables of the command name, the default keybinding, if any, and a short description of what the command does (see [Bindable Commands](#bindable-commands)).

Once you know the name of the command, simply place on a line in the init file the name of the key you wish to bind the command to, a colon, and then the name of the command. There can be no space between the key name and the colon (any space will be interpreted as part of the key name). The name of the key can be expressed in different ways, depending on what you find most comfortable.

In addition to command names, Readline allows keys to be bound to a string that is inserted when the key is pressed (a _macro_).

<table>
<tr><th>Line</th><th>Description</th></tr>
<tr><td><code><span class="arg">keyname</span>: <span class="arg">command</span></code></td><td>Binds a named command to a key.</td></tr>
<tr><td><code><span class="arg">keyname</span>: "<span class="arg">literal text</span>"</code></td><td>Binds a macro to a key.  A macro inserts the literal text into the input line.</td></tr>
<tr><td><code><span class="arg">keyname</span>: "luafunc:<span class="arg">lua_function_name</span>"</code></td><td>Binds a named Lua function to a key.  See <a href="#luakeybindings">Lua key bindings</a> for more information.</td></tr>
</table>

<p>
<dt>Key names</dt>
Key names can be a _name_ or a _sequence_.  Names are not quoted, and sequences are quoted.
</p>

_Names_ can be used to refer to simple keys like `Space`, `Return`, `Tab`, letters and digits (`A`, `b`, `1`, ...), and most punctuation (`!`, `@`, `.`, `_`, ...).  Names can also include modifier prefixes `C-` or `Control-` for the <kbd>Ctrl</kbd> key, or `M-` or `Meta-` for the Meta or <kbd>Alt</kbd> key.  However, modifier prefixes don't work with simple key names; you can't use `C-Space`, instead a _sequence_ is needed for special keys like that.

_Sequences_ are surrounded by double quotes, and specify an entire sequence of input characters.  Some special escape codes can be used:

Code | Description
-|-
`\C-` | Prefix meaning <kbd>Ctrl</kbd>.
`\M-` | Prefix meaning Meta or <kbd>Alt</kbd>.
`\e` | The literal ESC (escape) character code, which is the first character code in most special key sequences.<br/>Note: the ESC code isn't necessarily the same as the <kbd>Esc</kbd> key; see [`terminal.raw_esc`](#terminal_raw_esc).
`\\` | Backslash.
`\"` | `"`, a double quotation mark.
`\'` | `'`, a single quote or apostrophe.
`\a` | Alert (bell).
`\b` | Backspace.
`\d` | Delete. (Note: this is not very useful for Clink; it is not the <kbd>Del</kbd> key.)
`\f` | Form feed.
`\n` | Newline.
`\r` | Carriage return.
`\t` | Horizontal tab.
`\v` | Vertical tab.
<code>\<span class="arg">nnn</span></code> | The eight-bit character whose value is the octal value _nnn_ (one to three digits)
<code>\x<span class="arg">HH</span></code> | The eight-bit character whose value is the hexadecimal value _HH_ (one or two hex digits)

Here are some examples to illustrate the differences between _names_ and _sequences_:

Name | Sequence | Description
-|-|-
`C-a` | `"\C-a"` | Both refer to <kbd>Ctrl</kbd>-<kbd>a</kbd>.
`M-a` | `"\M-a"` | Both refer to <kbd>Alt</kbd>-<kbd>a</kbd>.
`M-C-a` | `"\M-\C-a"` | Both refer to <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>a</kbd>.
`hello` | | It's just <kbd>h</kbd>.  It is not quoted, so it is a _name_.  The `ello` part is a syntax error and is silently discarded by Readline.
| `"hello"` | The series of five keys <kbd>h</kbd> <kbd>e</kbd> <kbd>l</kbd> <kbd>l</kbd> <kbd>o</kbd>.
`Space` | | The <kbd>Space</kbd> key.
| `"Space"` | The series of five keys <kbd>S</kbd> <kbd>p</kbd> <kbd>a</kbd> <kbd>c</kbd> <kbd>e</kbd>.

Special keys like <kbd>Up</kbd> are represented by VT220 escape codes such as`"\e[A"`.  See [Discovering Clink key sequences](#discovering-clink-key-sequences) and [Binding special keys](#specialkeys) for how to find the <span class="arg">keyname</span> for the key you want to bind.

<p>
<dt>Key bindings</dt>
Key bindings can be either functions or macros (literal text).  Functions are not quoted, and macros are quoted.
</p>

- `blah-blah` binds to a function named "blah-blah".
- `"blah-blah"` is a macro that inserts the literal text "blah-blah" into the line.

When entering the text of a macro, single or double quotes must be used to indicate a macro definition. Unquoted text is assumed to be a function name. In the macro body, the backslash escapes described above are expanded. Backslash will quote any other character in the macro text, including `"` and `'`. For example, the following binding will make pressing <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>\\</kbd> insert a single `\` into the line:

```plaintext
"\C-x\\": "\\"
```

<p>
<dt>Examples</dt>

```plaintext
# Using key names.
C-u: universal-argument         # Bind Ctrl-u to invoke the universal-argument command.
C-o: "> output"                 # Bind Ctrl-o to insert the text "> output" into the line.

# Using key sequences.
"\C-u": universal-argument      # Bind Ctrl-u to invoke the universal-argument command.
"\C-x\C-r": clink-reload        # Bind Ctrl-x,Ctrl-r to reload the configuration and Lua scripts for Clink.
"\eOP": clink-popup-show-help   # Bind F1 to invoke the clink-popup-show-help command.
```
</p>

See [Customizing Key Bindings](#keybindings) for more information about binding keys in Clink.

#### Conditional init constructs

Readline implements a facility similar in spirit to the conditional compilation features of the C preprocessor which allows key bindings and variable settings to be performed as the result of tests. There are four parser directives used.

<p><dt class="toppadding">$if</dt></p><dd>
<dd>

The `$if` construct allows bindings to be made based on the editing mode, the terminal being used, or the application using Readline. The text of the test, after any comparison operator, extends to the end of the line; unless otherwise noted, no characters are required to isolate it.

The `$if mode=` form of the `$if` directive is used to test whether Readline is in `emacs` or `vi` mode. This may be used in conjunction with the "set keymap" command, for instance, to set bindings in the `emacs-standard` and `emacs-ctlx` keymaps only if Readline is starting out in `emacs` mode. (The directive is only tested during startup.)

```plaintext
$if mode == emacs
set show-mode-in-prompt on
$endif
```

The `$if term=` form may be used to include terminal-specific key bindings, perhaps to bind the key sequences output by the terminal's function keys. The word on the right side of the "=" is tested against both the full name of the terminal and the portion of the terminal name before the first "-". This allows `sun` to match both `sun` and `sun-cmd`, for instance. This is not useful with Clink, because Clink has its own terminal driver.

The `$if version` test may be used to perform comparisons against specific Readline versions. The `version` expands to the current Readline version. The set of comparison operators includes `=` (and `==`), `!=`, `<=`, `>=`, `<`, and `>`. The version number supplied on the right side of the operator consists of a major version number, an optional decimal point, and an optional minor version (e.g., "7.1"). If the minor version is omitted, it is assumed to be "0". The operator may be separated from the string `version` and from the version number argument by whitespace. The following example sets a variable if the Readline version being used is 7.0 or newer:

```plaintext
$if version >= 7.0
set show-mode-in-prompt on
$endif
```

The `$if application` construct is used to include application-specific settings. Each program using the Readline library sets the _application name_, and you can test for a particular value. This could be used to bind key sequences to functions useful for a specific program. For instance, the following command adds a key sequence that quotes the current or previous word, but only in Clink:

```plaintext
$if clink
# Quote the current or previous word
"\C-xq": "\eb\"\ef\""
$endif
```

The `$if variable` construct provides simple equality tests for Readline variables and values. The permitted comparison operators are `=`, `==`, and `!=`. The variable name must be separated from the comparison operator by whitespace; the operator may be separated from the value on the right hand side by whitespace. Both string and boolean variables may be tested. Boolean variables must be tested against the values _on_ and _off_. The following example is equivalent to the `mode=emacs` test described above:

```plaintext
$if editing-mode == emacs
set show-mode-in-prompt on
$endif
```
</dd>

<p><dt class="toppadding">$endif</dt></p><dd>

This command, as seen in the previous example, terminates an `$if` command.
</dd>

<p><dt class="toppadding">$else</dt></p><dd>

Commands in this branch of the `$if` directive are executed if the test fails.
</dd>

<p><dt class="toppadding">$include</dt></p><dd>

This directive takes a single filename as an argument and reads commands and bindings from that file. For example, the following directive reads from "c:\dir\inputrc":

```plaintext
$include c:\dir\inputrc
```
</dd>

#### Sample .inputrc file

Here is a sample `.inputrc` file with some of the variables and key bindings that I use:

<pre><code class="plaintext"><span class="hljs-meta">$if clink</span>           <span class="hljs-comment"># begin clink-only section</span>

<span class="hljs-meta">set colored-completion-prefix                       on</span>
<span class="hljs-meta">set colored-stats                                   on</span>
<span class="hljs-meta">set mark-symlinked-directories                      on</span>
<span class="hljs-meta">set visible-stats                                   off</span>
<span class="hljs-meta">set completion-auto-query-items                     on</span>
<span class="hljs-meta">set history-point-at-end-of-anchored-search         on</span>
<span class="hljs-meta">set menu-complete-wraparound                        off</span>
<span class="hljs-meta">set search-ignore-case                              on</span>

<span class="hljs-comment"># The following key bindings are for emacs mode.</span>
<span class="hljs-meta">set keymap emacs</span>

<span class="hljs-string">"\e[27;8;72~"</span>:      clink-popup-show-help           <span class="hljs-comment"># Alt-Ctrl-Shift-H</span>

<span class="hljs-comment"># Completion key bindings.</span>
<span class="hljs-string">"\t"</span>:               old-menu-complete               <span class="hljs-comment"># Tab</span>
<span class="hljs-string">"\e[Z"</span>:             old-menu-complete-backward      <span class="hljs-comment"># Shift-Tab</span>
<span class="hljs-string">"\e[27;5;9~"</span>:       clink-popup-complete            <span class="hljs-comment"># Ctrl-Tab</span>

<span class="hljs-comment"># Some key bindings I got used to from 4Dos/4NT/Take Command.</span>
C-b:                                                <span class="hljs-comment"># Ctrl-B (cleared because I redefined Ctrl-F)</span>
C-d:                remove-history                  <span class="hljs-comment"># Ctrl-D (replaces `delete-char`)</span>
C-f:                clink-expand-doskey-alias       <span class="hljs-comment"># Ctrl-F (replaces `forward-char`)</span>
C-k:                add-history                     <span class="hljs-comment"># Ctrl-K (replaces `kill-line`)</span>
<span class="hljs-string">"\e[A"</span>:             history-search-backward         <span class="hljs-comment"># Up (replaces `previous-history`)</span>
<span class="hljs-string">"\e[B"</span>:             history-search-forward          <span class="hljs-comment"># Down (replaces `next-history`)</span>
<span class="hljs-string">"\e[5~"</span>:            clink-popup-history             <span class="hljs-comment"># PgUp (replaces `history-search-backward`)</span>
<span class="hljs-string">"\e[6~"</span>:                                            <span class="hljs-comment"># PgDn (cleared because I redefined PgUp)</span>
<span class="hljs-string">"\e[1;5F"</span>:          end-of-line                     <span class="hljs-comment"># Ctrl-End (replaces `kill-line`)</span>
<span class="hljs-string">"\e[1;5H"</span>:          beginning-of-line               <span class="hljs-comment"># Ctrl-Home (replaces `backward-kill-line`)</span>

<span class="hljs-comment"># Some key bindings handy in default (conhost) console windows.</span>
M-b:                                                <span class="hljs-comment"># Alt-B (cleared because I redefined Alt-F)</span>
M-f:                clink-find-conhost              <span class="hljs-comment"># Alt-F for "Find..." from the console's system menu</span>
M-m:                clink-mark-conhost              <span class="hljs-comment"># Alt-M for "Mark" from the console's system menu</span>

<span class="hljs-comment"># Some key bindings for interrogating the Readline configuration.</span>
<span class="hljs-string">"\C-x\C-f"</span>:         dump-functions                  <span class="hljs-comment"># Ctrl-X, Ctrl-F</span>
<span class="hljs-string">"\C-x\C-m"</span>:         dump-macros                     <span class="hljs-comment"># Ctrl-X, Ctrl-M</span>
<span class="hljs-string">"\C-x\C-v"</span>:         dump-variables                  <span class="hljs-comment"># Ctrl-X, Ctrl-V</span>

<span class="hljs-comment"># Misc other key bindings.</span>
<span class="hljs-string">"\e[27;2;32~"</span>:      clink-magic-suggest-space       <span class="hljs-comment"># Shift-Space</span>
<span class="hljs-string">"\e[5;6~"</span>:          clink-popup-directories         <span class="hljs-comment"># Ctrl-Shift-PgUp</span>
C-_:                kill-line                       <span class="hljs-comment"># Ctrl-- (replaces `undo`)</span>

<span class="hljs-meta">$endif</span>              <span class="hljs-comment"># end clink-only section</span>
</code></pre>

## Bindable Commands

### Commands For Moving

Command | Key | Description
-|:-:|-
`beginning-of-line` | <kbd>Home</kbd> | Move to the start of the current line.
`end-of-line` | <kbd>End</kbd> | Move to the end of the line.
`forward-char` | <kbd>Right</kbd> | Move forward a character.
`backward-char` | <kbd>Left</kbd> | Move back a character.
`forward-word` | <kbd>Ctrl</kbd>-<kbd>Right</kbd> | Move forward to the end of the next word. Words are composed of letters and digits.
`backward-word` | <kbd>Ctrl</kbd>-<kbd>Left</kbd> | Move back to the start of the current or previous word. Words are composed of letters and digits.
`previous-screen-line` | | Attempt to move point to the same physical screen column on the previous physical screen line. This will not have the desired effect if the current Readline line does not take up more than one physical line or if point is not greater than the length of the prompt plus the screen width.
`next-screen-line` | | Attempt to move point to the same physical screen column on the next physical screen line. This will not have the desired effect if the current Readline line does not take up more than one physical line or if the length of the current Readline line is not greater than the length of the prompt plus the screen width.
`clear-display` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>l</kbd> | Clear the screen and, if possible, the terminal's scrollback buffer, then redraw the current line, leaving the current line at the top of the screen.
`clear-screen` | <kbd>Ctrl</kbd>-<kbd>l</kbd> | Clear the screen, then redraw the current line, leaving the current line at the top of the screen.
`redraw-current-line` | | Refresh the current line. By default, this is unbound.

### Commands For Manipulating The History

Command | Key | Description
-|:-:|-
`accept-line` | <kbd>Enter</kbd> | Accept the line regardless of where the cursor is. If this line is non-empty, it may be added to the history list for future recall.
`previous-history` | <kbd>Ctrl</kbd>-<kbd>p</kbd> | Move "back" through the history list, fetching the previous command.
`next-history` | <kbd>Ctrl</kbd>-<kbd>n</kbd> | Move "forward" through the history list, fetching the next command.
`beginning-of-history` | <kbd>Alt</kbd>-<kbd><</kbd> | Move to the first line in the history.
`end-of-history` | <kbd>Alt</kbd>-<kbd>></kbd> | Move to the end of the input history, i.e., the line currently being entered.
`reverse-search-history` | <kbd>Ctrl</kbd>-<kbd>r</kbd> | Search backward starting at the current line and moving "up" through the history as necessary. This is an incremental search. This command sets the region to the matched text and activates the mark.
`forward-search-history` | <kbd>Ctrl</kbd>-<kbd>s</kbd> | Search forward starting at the current line and moving "down" through the history as necessary. This is an incremental search. This command sets the region to the matched text and activates the mark.
`non-incremental-reverse-search-history` | <kbd>Alt</kbd>-<kbd>p</kbd> | Search backward starting at the current line and moving "up" through the history as necessary using a non-incremental search for a string supplied by the user. The search string may match anywhere in a history line.
`non-incremental-forward-search-history` | <kbd>Alt</kbd>-<kbd>n</kbd> | Search forward starting at the current line and moving "down" through the history as necessary using a non-incremental search for a string supplied by the user. The search string may match anywhere in a history line.
`history-search-forward` | | Search forward through the history for the string of characters between the start of the current line and the point. The search string must match at the beginning of a history line. This is a non-incremental search. By default, this command is unbound.
`history-search-backward` | | Search backward through the history for the string of characters between the start of the current line and the point. The search string must match at the beginning of a history line. This is a non-incremental search. By default, this command is unbound.
`history-substring-search-forward` | | Search forward through the history for the string of characters between the start of the current line and the point. The search string may match anywhere in a history line. This is a non-incremental search. By default, this command is unbound.
`history-substring-search-backward` | | Search backward through the history for the string of characters between the start of the current line and the point. The search string may match anywhere in a history line. This is a non-incremental search. By default, this command is unbound.
`yank-nth-arg` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>y</kbd> | Insert the first argument to the previous command (usually the second word on the previous line) at point. With an argument _n_, insert the <em>n</em>th word from the previous command (the words in the previous command begin with word 0). A negative argument inserts the <em>n</em>th word from the end of the previous command. Once the argument _n_ is computed, the argument is extracted as if the "!n" history expansion had been specified.
`yank-last-arg` | <kbd>Alt</kbd>-<kbd>.</kbd> or <kbd>Alt</kbd>-<kbd>_</kbd> | Insert last argument to the previous command (the last word of the previous history entry). With a numeric argument, behave exactly like `yank-nth-arg`. Successive calls to `yank-last-arg` move back through the history list, inserting the last word (or the word specified by the argument to the first call) of each line in turn. Any numeric argument supplied to these successive calls determines the direction to move through the history. A negative argument switches the direction through the history (back or forward). The history expansion facilities are used to extract the last argument, as if the "!$" history expansion had been specified.
`operate-and-get-next` | <kbd>Ctrl</kbd>-<kbd>o</kbd> | Accept the current line for return to the calling application as if a newline had been entered, and fetch the next line relative to the current line from the history for editing. A numeric argument, if supplied, specifies the history entry to use instead of the current line.

### Commands For Changing Text

Command | Key | Description
-|:-:|-
`delete-char` | <kbd>Ctrl</kbd>-<kbd>d</kbd> | Delete the character at point.<br/>Note: also see the [`cmd.ctrld_exits`](#ctrld_exits) setting.
`backward-delete-char` | <kbd>Backspace</kbd> | Delete the character behind the cursor. A numeric argument means to kill the characters instead of deleting them.
`forward-backward-delete-char` | | Delete the character under the cursor, unless the cursor is at the end of the line, in which case the character behind the cursor is deleted. By default, this is not bound to a key.
`quoted-insert` | <kbd>Ctrl</kbd>-<kbd>q</kbd> | Add the next character typed to the line verbatim. This is how to insert key sequences like <kbd>Ctrl</kbd>-<kbd>h</kbd> or <kbd>Tab</kbd>, for example.
`tab-insert` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>i</kbd> | Insert a tab character.
`self-insert` | <kbd>a</kbd>, <kbd>b</kbd>, <kbd>A</kbd>, <kbd>1</kbd>, <kbd>!</kbd>, etc | Insert the key itself.
`bracketed-paste-begin` | | This function is intended to be bound to the "bracketed paste" escape sequence sent by some terminals, and such a binding is assigned by default. It allows Readline to insert the pasted text as a single unit without treating each character as if it had been read from the keyboard. The characters are inserted as if each one was bound to `self-insert` instead of executing any editing commands.<br/>Bracketed paste sets the region (the characters between point and the mark) to the inserted text. It uses the concept of an active mark: when the mark is active, Readline redisplay uses the terminal's standout mode to denote the region.
`transpose-chars` | <kbd>Ctrl</kbd>-<kbd>t</kbd> | Drag the character before the cursor forward over the character at the cursor, moving the cursor forward as well. If the insertion point is at the end of the line, then this transposes the last two characters of the line. Negative arguments have no effect.
`transpose-words` | <kbd>Alt</kbd>-<kbd>t</kbd> | Drag the word before point past the word after point, moving point past that word as well. If the insertion point is at the end of the line, this transposes the last two words on the line.
`upcase-word` | <kbd>Alt</kbd>-<kbd>u</kbd> | Uppercase the current (or following) word. With a negative argument, uppercase the previous word, but do not move the cursor.
`downcase-word` | <kbd>Alt</kbd>-<kbd>l</kbd> | Lowercase the current (or following) word. With a negative argument, lowercase the previous word, but do not move the cursor.
`capitalize-word` | | Capitalize the current (or following) word. With a negative argument, capitalize the previous word, but do not move the cursor.
`overwrite-mode` | | Toggle overwrite mode. With an explicit positive numeric argument, switches to overwrite mode. With an explicit non-positive numeric argument, switches to insert mode. This command affects only emacs mode; vi mode does overwrite differently. Each new command line prompt starts in insert mode.<br/>In overwrite mode, characters bound to `self-insert` replace the text at point rather than pushing the text to the right. Characters bound to `backward-delete-char` replace the character before point with a space.<br/>By default, this command is unbound.

### Killing And Yanking

Command | Key | Description
-|:-:|-
`kill-line` | <kbd>Ctrl</kbd>-<kbd>k</kbd> | Kill the text from point to the end of the line. With a negative numeric argument, kill backward from the cursor to the beginning of the current line.
`backward-kill-line` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>Backspace</kbd> | Kill backward from the cursor to the beginning of the current line. With a negative numeric argument, kill forward from the cursor to the end of the current line.
`unix-line-discard` | <kbd>Ctrl</kbd>-<kbd>u</kbd> | Kill backward from the cursor to the beginning of the current line.
`kill-whole-line` | | Kill all characters on the current line, no matter where point is. By default, this is unbound.
`kill-word` | <kbd>Alt</kbd>-<kbd>d</kbd> | Kill from point to the end of the current word, or if between words, to the end of the next word. Word boundaries are the same as `forward-word`.
`backward-kill-word` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>Backspace</kbd> | Kill the word behind point. Word boundaries are the same as `backward-word`.
`unix-word-rubout` | <kbd>Ctrl</kbd>-<kbd>w</kbd> | Kill the word behind point, using white space as a word boundary. The killed text is saved on the kill-ring.
`unix-filename-rubout` | | Kill the word behind point, using white space and the slash character as the word boundaries. The killed text is saved on the kill-ring.
`delete-horizontal-space` | | Delete all spaces and tabs around point. By default, this is unbound.
`kill-region` | | Kill the text in the current region. By default, this command is unbound.
`copy-region-as-kill` | | Copy the text in the region to the kill buffer, so it can be yanked right away. By default, this command is unbound.
`copy-backward-word` | | Copy the word before point to the kill buffer. The word boundaries are the same as `backward-word`. By default, this command is unbound.
`copy-forward-word` | | Copy the word following point to the kill buffer. The word boundaries are the same as `forward-word`. By default, this command is unbound.
`yank` | <kbd>Ctrl</kbd>-<kbd>y</kbd> | Yank the top of the kill ring into the buffer at point.
`yank-pop` | <kbd>Alt</kbd>-<kbd>y</kbd> | Rotate the kill-ring, and yank the new top. You can only do this if the prior command is `yank` or `yank-pop`.

### Specifying Numeric Arguments

Command | Key | Description
-|:-:|-
`digit-argument` | <kbd>Alt</kbd>-<kbd><em>digit</em></kbd> or <kbd>Alt</kbd>-<kbd>-</kbd> | Add this <em>digit</em> to the argument already accumulating, or start a new argument. <kbd>Alt</kbd>-<kbd>-</kbd> starts a negative argument.
`universal-argument` | | This is another way to specify an argument. If this command is followed by one or more digits, optionally with a leading minus sign, those digits define the argument. If the command is followed by digits, executing `universal-argument` again ends the numeric argument, but is otherwise ignored. As a special case, if this command is immediately followed by a character that is neither a digit nor minus sign, the argument count for the next command is multiplied by four. The argument count is initially one, so executing this function the first time makes the argument count four, a second time makes the argument count sixteen, and so on. By default, this is not bound to a key.

### Completion Commands

Command | Key | Description
-|:-:|-
`complete` | <kbd>Tab</kbd> | Attempt to perform completion on the text before point.
`possible-completions` | <kbd>Alt</kbd>-<kbd>=</kbd> | List the possible completions of the text before point. When displaying completions, Readline sets the number of columns used for display to the value of [`completion-display-width`](#configcompletiondisplaywidth), the value of the environment variable `%COLUMNS%`, or the screen width, in that order.
`insert-completions` | <kbd>Alt</kbd>-<kbd>*</kbd> | Insert all completions of the text before point that would have been generated by `possible-completions`.
`menu-complete` | | Similar to `complete`, but replaces the word to be completed with a single match from the list of possible completions. Repeated execution of `menu-complete` steps through the list of possible completions, inserting each match in turn. At the end of the list of completions, the bell is rung (subject to the setting of [`bell-style`](#configbellstyle)) and the original text is restored. An argument of _n_ moves _n_ positions forward in the list of matches; a negative argument may be used to move backward through the list. This command is intended to be bound to <kbd>TAB</kbd>, but is unbound by default.
`menu-complete-backward` | | Identical to `menu-complete`, but moves backward through the list of possible completions, as if `menu-complete` had been given a negative argument.
`old-menu-complete` | | Similar to `menu-complete` but isn't limited by [`completion-query-items`](#configcompletionqueryitems) and doesn't include the common prefix of the possible completions. This behaves like the default completion in cmd.exe on Windows.
`delete-char-or-list` | | Deletes the character under the cursor if not at the beginning or end of the line (like `delete-char`). If at the end of the line, behaves identically to `possible-completions`. This command is unbound by default.

### Keyboard Macros

Command | Key | Description
-|:-:|-
`start-kbd-macro` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>(</kbd> | Begin saving the characters typed into the current keyboard macro.
`end-kbd-macro` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>)</kbd> | Stop saving the characters typed into the current keyboard macro and save the definition.
`call-last-kbd-macro` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>e</kbd> | Re-execute the last keyboard macro defined, by making the characters in the macro appear as if typed at the keyboard.
`print-last-kbd-macro` | | Print the last keboard macro defined in a format suitable for the inputrc file.

### Some Miscellaneous Commands

Command | Key | Description
-|:-:|-
`re-read-init-file` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>r</kbd> | Read in the contents of the inputrc file, and incorporate any bindings or variable assignments found there.
`abort` | <kbd>Ctrl</kbd>-<kbd>g</kbd> | Abort the current editing command and ring the terminal's bell (subject to the setting of [`bell-style`](#configbellstyle)).
`do-lowercase-version` | <kbd>Alt</kbd>-<kbd><em>X</em></kbd>, etc | If the key <kbd><em>X</em></kbd> is an upper case letter, run the command that is bound to the corresponding <kbd>Alt</kbd>-<kbd><em>x</em></kbd> lower case letter. The behavior is undefined if <em>x</em> is already lower case.
`undo` | <kbd>Ctrl</kbd>-<kbd>z</kbd> or <kbd>Ctrl</kbd>-<kbd>_</kbd> | Incremental undo, separately remembered for each line.
`revert-line` | <kbd>Alt</kbd>-<kbd>r</kbd> | Undo all changes made to this line. This is like executing the `undo` command enough times to get back to the beginning.
`tilde-expand` | <kbd>Alt</kbd>-<kbd>~</kbd> | Perform tilde expansion on the current word.
`set-mark` | <kbd>Ctrl</kbd>-<kbd>@</kbd> | Set the mark to the point. If a numeric argument is supplied, the mark is set to that position.
`exchange-point-and-mark` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>x</kbd> | Swap the point with the mark. The current cursor position is set to the saved position, and the old cursor position is saved as the mark.
`character-search` | <kbd>Ctrl</kbd>-<kbd>]</kbd> | A character is read and point is moved to the next occurrence of that character. A negative count searches for previous occurrences.
`character-search-backward` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>]</kbd> | A character is read and point is moved to the previous occurrence of that character. A negative count searches for subsequent occurrences.
`insert-comment` | <kbd>Alt</kbd>-<kbd>#</kbd> | Without a numeric argument, the value of the [`comment-begin`](#configcommentbegin) variable is inserted at the beginning of the current line. If a numeric argument is supplied, this command acts as a toggle: if the characters at the beginning of the line do not match the value of `comment-begin`, the value is inserted, otherwise the characters in `comment-begin` are deleted from the beginning of the line. In either case, the line is accepted as if a newline had been typed.
`dump-functions` | | Print all of the functions and their key bindings to the Readline output stream. If a numeric argument is supplied, the output is formatted in such a way that it can be made part of an inputrc file. This command is unbound by default.
`dump-variables` | | Print all of the settable variables and their values to the Readline output stream. If a numeric argument is supplied, the output is formatted in such a way that it can be made part of an inputrc file. This command is unbound by default.
`dump-macros` | | Print all of the Readline key sequences bound to macros and the strings they output. If a numeric argument is supplied, the output is formatted in such a way that it can be made part of an inputrc file. This command is unbound by default.

### Readline vi Mode

While the Readline library does not have a full set of vi editing functions, it does contain enough to allow simple editing of the line. The Readline vi mode behaves as specified in the POSIX standard.

In order to switch interactively between emacs and vi editing modes, use the command <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>j</kbd> (bound to `emacs-editing-mode` when in vi mode and to `vi-editing-mode` in emacs mode). The Readline default is emacs mode.

When you enter a line in vi mode, you are already placed in "insertion" mode, as if you had typed an "i". Pressing <kbd>Esc</kbd> switches you into "command" mode, where you can edit the text of the line with the standard vi movement keys, move to previous history lines with "k" and subsequent lines with "j", and so forth.

Command | Key | Description
-|:-:|-
`emacs-editing-mode` | <kbd>Ctrl</kbd>-<kbd>e</kbd> | When in vi command mode, this causes a switch to emacs editing mode.
`vi-editing-mode` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>j</kbd> | When in emacs editing mode, this causes a switch to vi editing mode.

### Other Readline Commands

These other commands are not very useful in Clink, but exist nevertheless.

Command | Key | Description
-|:-:|-
`prefix-meta` | | "Metafy" the next character typed. This is for keyboards without an <kbd>Alt</kbd> meta key. Typing a key bound to `prefix-meta` and then <kbd>f</kbd> is equivalent to typing <kbd>Alt</kbd>-<kbd>f</kbd>. By default this is bound to <kbd>Esc</kbd>, but only when the <a href="#terminal_raw_esc">`terminal.raw_esc`</a> Clink setting is enabled.
`skip-csi-sequence` | | This has no effect unless the <a href="#terminal_raw_esc">`terminal.raw_esc`</a> Clink setting is enabled. Reads enough characters to consume a multi-key sequence such as those defined for keys like <kbd>Home</kbd> and <kbd>End</kbd>. Such sequences begin with a Control Sequence Indicator (CSI), which is `ESC` `[`. If this sequence is bound to "\e[", keys producing such sequences will have no effect unless explicitly bound to a readline command, instead of inserting stray characters into the editing buffer. This is unbound by default.

### Clink Commands

Clink also adds some new commands, beyond what's normally provided by Readline.

Command | Key | Description
-|:-:|-
`add-history` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>k</kbd> | Adds the current line to the history without executing it, and clears the editing line.
`alias-expand-line` | | A synonym for `clink-expand-doskey-alias`.
`clink-accept-suggestion` | | If there is a suggestion, insert the suggestion and accept the input line (like `accept-line`).
`clink-complete-numbers` | | Like `complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-copy-cwd` | <kbd>Alt</kbd>-<kbd>c</kbd> | Copy the current working directory to the clipboard.
`clink-copy-line` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>c</kbd> | Copy the current line to the clipboard.
`clink-copy-word` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>w</kbd> | Copy the word at the cursor to the clipboard, or copies the <em>n</em>th word if a numeric argument is provided via the `digit-argument` keys.
`clink-ctrl-c` | <kbd>Ctrl</kbd>-<kbd>c</kbd> | Discards the current line and starts a new one (like <kbd>Ctrl</kbd>-<kbd>C</kbd> in CMD.EXE).
`clink-diagnostics` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>z</kbd> | Show internal diagnostic information.
`clink-exit` | <kbd>Alt</kbd>-<kbd>F4</kbd> | Replaces the current line with `exit` and executes it (exits the shell instance).
`clink-expand-doskey-alias` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>f</kbd> | Expand the doskey alias (if any) at the beginning of the line.
`clink-expand-env-var` | | Expand the environment variable (e.g. `%FOOBAR%`) at the cursor.
`clink-expand-history` | <kbd>Alt</kbd>-<kbd>^</kbd> | Perform [history](#using-history-expansion) expansion in the current input line.
`clink-expand-history-and-alias` | | Perform [history](#using-history-expansion) and doskey alias expansion in the current input line.
`clink-expand-line` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>e</kbd> | Perform [history](#using-history-expansion), doskey alias, and environment variable expansion in the current input line.
`clink-find-conhost` | | Activates the "Find" dialog when running in a standard console window (hosted by the OS conhost).  This is equivalent to picking "Find..." from the console window's system menu. When [`clink.default_bindings`](#default_bindings) is enabled, this is bound to <kbd>Ctrl</kbd>-<kbd>f</kbd>.
`clink-insert-dot-dot` | <kbd>Alt</kbd>-<kbd>a</kbd> | Inserts `..\` at the cursor.
`clink-insert-suggestion` | | If there is a suggestion, insert the suggestion.
`clink-magic-suggest-space` | | Inserts the next full word of the suggestion (if any) up to a space, and also inserts a space.
`clink-mark-conhost` | | Activates the "Mark" mode when running in a standard console window (hosted by the OS conhost).  This is equivalent to picking "Mark" from the console window's system menu. When [`clink.default_bindings`](#default_bindings) is enabled, this is bound to <kbd>Ctrl</kbd>-<kbd>m</kbd>.
`clink-menu-complete-numbers` | | Like `menu-complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-menu-complete-numbers-backward` | | Like `menu-complete-backward`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-old-menu-complete-numbers` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>n</kbd> | Like `old-menu-complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-old-menu-complete-numbers-backward` | | Like `old-menu-complete-backward`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-paste` | <kbd>Ctrl</kbd>-<kbd>v</kbd> | Paste the clipboard at the cursor.
`clink-popup-complete` | | Show a [popup window](#popupwindow) that lists the available completions.
`clink-popup-complete-numbers` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>Shift</kbd>-<kbd>N</kbd> | Like `clink-popup-complete`, but for numbers from the console screen (3 digits or more, up to hexadecimal).
`clink-popup-directories` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>PgUp</kbd> | Show a [popup window](#popupwindow) of recent current working directories.  In the popup, use <kbd>Enter</kbd> to `cd /d` to the highlighted directory.
`clink-popup-history` | | Show a [popup window](#popupwindow) that lists the command history (if any text precedes the cursor then it uses an anchored search to filter the list).  In the popup, use <kbd>Enter</kbd> to execute the highlighted command.
`clink-popup-show-help` | | Show a [popup window](#popupwindow) that lists the currently active key bindings, and can invoke a selected key binding.  The default key binding for this is <kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>H</kbd>.
`clink-reload` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>r</kbd> | Reloads the .inputrc file and the Lua scripts.
`clink-reset-line` | <kbd>Esc</kbd> | Clears the current line.
`clink-scroll-bottom` | <kbd>Alt</kbd>-<kbd>End</kbd> | Scroll the console window to the bottom (the current input line).
`clink-scroll-line-down` | <kbd>Alt</kbd>-<kbd>Down</kbd> | Scroll the console window down one line.
`clink-scroll-line-up` | <kbd>Alt</kbd>-<kbd>Up</kbd> | Scroll the console window up one line.
`clink-scroll-page-down` | <kbd>Alt</kbd>-<kbd>PgDn</kbd> | Scroll the console window down one page.
`clink-scroll-page-up` | <kbd>Alt</kbd>-<kbd>PgUp</kbd> | Scroll the console window up one page.
`clink-scroll-top` | <kbd>Alt</kbd>-<kbd>Home</kbd> | Scroll the console window to the top.
`clink-select-complete` | <kbd>Ctrl</kbd>-<kbd>Space</kbd> | Like `complete`, but shows interactive menu of matches and responds to arrow keys and typing to filter the matches.  While completing, <kbd>F1</kbd> toggles showing match descriptions at the bottom vs next to each match.
`clink-selectall-conhost` | | Mimics the "Select All" command when running in a standard console window (hosted by the OS conhots).  Selects the input line text.  If already selected, then it invokes the "Select All" command from the console window's system menu and selects the entire screen buffer's contents. When [`clink.default_bindings`](#default_bindings) is enabled, this is bound to <kbd>Ctrl</kbd>-<kbd>a</kbd>.
`clink-shift-space` | <kbd>Shift</kbd>-<kbd>Space</kbd> | Invokes the normal <kbd>Space</kbd> key binding, so that <kbd>Shift</kbd>-<kbd>Space</kbd> behaves the same as <kbd>Space</kbd>.
`clink-show-help` | <kbd>Alt</kbd>-<kbd>h</kbd> | Lists the currently active key bindings using friendly key names.  A numeric argument affects showing categories and descriptions:  0 for neither, 1 for categories, 2 for descriptions, 3 for categories and descriptions (the default), 4 for all commands (even if not bound to a key).
`clink-show-help-raw` | | Lists the currently active key bindings using raw key sequences.  A numeric argument affects showing categories and descriptions:  0 for neither, 1 for categories, 2 for descriptions, 3 for categories and descriptions (the default), 4 for all commands (even if not bound to a key).
`clink-up-directory` | <kbd>Ctrl</kbd>-<kbd>PgUp</kbd> | Changes to the parent directory.
`clink-what-is` | <kbd>Alt</kbd>-<kbd>Shift</kbd>-<kbd>/</kbd> | Show the key binding for the next key sequence that is input.
`cua-backward-char` | <kbd>Shift</kbd>-<kbd>Left</kbd> | Extends the selection and moves back a character.
`cua-backward-word` | <kbd>Ctrl</kbd>-<kbd>Shift</kbd>-<kbd>Left</kbd> | Extends the selection and moves back a word.
`cua-beg-of-line` | <kbd>Shift</kbd>-<kbd>Home</kbd> | Extends the selection and moves to the start of the current line.
`cua-copy` | <kbd>Shift</kbd>-<kbd>Ins</kbd> | Copies the selection to the clipboard.
`cua-cut` | <kbd>Shift</kbd>-<kbd>Del</kbd> | Cuts the selection to the clipboard.
`cua-end-of-line` | <kbd>Shift</kbd>-<kbd>End</kbd> | Extends the selection and moves to the end of the line.
`cua-forward-char` | <kbd>Shift</kbd>-<kbd>Right</kbd> | Extends the selection and moves forward a character, or inserts the next full suggested word up to a space.
`cua-forward-word` | <kbd>Ctrl</kbd>-<kbd>Shift</kbd>-<kbd>Right</kbd> | Extends the selection and moves forward a word.
`cua-next-screen-line` | <kbd>Shift</kbd>-<kbd>Down</kbd> | Extends the selection down one line.
`cua-previous-screen-line` | <kbd>Shift</kbd>-<kbd>Up</kbd> | Extends the selection up one line.
`cua-select-all` | | Extends the selection to the entire current line.
`cua-select-word` | | Selects the word at the cursor.
`edit-and-execute-command` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>Ctrl</kbd>-<kbd>e</kbd> | Invoke an editor on the current input line, and execute the result as commands.  This attempts to invoke `%VISUAL%`, `%EDITOR%`, or `notepad.exe` as the editor, in that order.
`glob-complete-word` | <kbd>Alt</kbd>-<kbd>g</kbd> | Perform wildcard completion on the text before the cursor point, with a `*` implicitly appended.
`glob-expand-word` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>*</kbd> | Insert all the wildcard completions that `glob-list-expansions` would list.  If a numeric argument is supplied, a `*` is implicitly appended before completion.
`glob-list-expansions` | <kbd>Ctrl</kbd>-<kbd>x</kbd> <kbd>g</kbd> | List the possible wildcard completions of the text before the cursor point.  If a numeric argument is supplied, a `*` is implicitly appended before completion.
`history-and-alias-expand-line` | | A synonym for `clink-expand-history-and-alias`.
`history-expand-line` | | A synonym for `clink-expand-history`.
`insert-last-argument` | | A synonym for `yank-last-arg`.
`magic-space` | | Perform [history](#using-history-expansion) expansion on the text before the cursor position and insert a space.
`old-menu-complete-backward` | | Like `old-menu-complete`, but in reverse.
`remove-history` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>d</kbd> | While searching history, removes the current line from the history.
`shell-expand-line` | <kbd>Alt</kbd>-<kbd>Ctrl</kbd>-<kbd>e</kbd> | A synonym for `clink-expand-line`.
`win-copy-history-number` | <kbd>F9</kbd> | Enter a history number and replace the input line with the history line (mimics Windows console <kbd>F9</kbd>).
`win-copy-up-to-char` | <kbd>F2</kbd> | Enter a character and copy up to it from the previous command (mimics Windows console <kbd>F2</kbd>).
`win-copy-up-to-end` | <kbd>F3</kbd> | Copy the rest of the previous command (mimics Windows console <kbd>F3</kbd>).
`win-cursor-forward` | <kbd>F1</kbd> | Move cursor forward, or at end of line copy character from previous command, or insert suggestion (mimics Windows console <kbd>F1</kbd> and <kbd>Right</kbd>).
`win-delete-up-to-char` | <kbd>F4</kbd> | Enter a character and delete up to it in the input line (mimics Windows console <kbd>F4</kbd>).
`win-history-list` | <kbd>F7</kbd> | Executes a history entry from a list (mimics Windows console <kbd>F7</kbd>).
`win-insert-eof` | <kbd>F6</kbd> | Insert ^Z (mimics Windows console <kbd>F6</kbd>).

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

For example, `win-history-list` (<kbd>F7</kbd>) and `clink-popup-directories` (<kbd>Ctrl</kbd>-<kbd>Alt</kbd>-<kbd>PgUp</kbd>) show popup windows.

Here's how the popup windows work:

Key | Description
:-:|---
<kbd>Escape</kbd>|Cancels the popup.
<kbd>Enter</kbd>|Inserts the highlighted completion, changes to the highlighted directory, or executes the highlighted command.
<kbd>Shift</kbd>-<kbd>Enter</kbd>|Inserts the highlighted completion, inserts the highlighted directory, or jumps to the highlighted command history entry without executing it.
<kbd>Ctrl</kbd>-<kbd>Enter</kbd>|Same as <kbd>Shift</kbd>-<kbd>Enter</kbd>.
<kbd>Del</kbd>|In a command history popup, <kbd>Del</kbd> deletes the selected history entry.

Most of the popup windows also have incremental search:

Key | Description
:-:|---
Typing|Typing does an incremental search.
<kbd>F3</kbd>|Go to the next match.
<kbd>Ctrl</kbd>-<kbd>L</kbd>|Go to the next match.
<kbd>Shift</kbd>-<kbd>F3</kbd>|Go to the previous match.
<kbd>Ctrl</kbd>-<kbd>Shift</kbd>-<kbd>L</kbd>|Go to the previous match.

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

Lua scripts are loaded once and are only reloaded if forced because the scripts locations change, the `clink-reload` command is invoked (<kbd>Ctrl</kbd>-<kbd>X</kbd>,<kbd>Ctrl</kbd>-<kbd>R</kbd>), or the `lua.reload_scripts` setting changes (or is True).

Run `clink info` to see the script paths for the current session.

### Completion directories

You may optionally put Lua completion scripts in a `completions` directory.  That prevents them from being loaded when Clink starts, and instead they are only loaded when needed.  That can make Clink load faster if you have a large quantity of Lua scripts that define argmatchers.

When a command name is typed, if a corresponding argmatcher is not already loaded then the completions directories are searched for a Lua script by the same name.  If found, then the Lua script is loaded.  This is similar to how completion scripts work in shells like bash, zsh, and fish.

For example, if you type `xyz` and an argmatcher for `xyz` is not yet loaded, then if `xyz.lua` exists in one of the completions directories it will be loaded.

Clink looks for completion scripts in these directories:
1. Any directories listed in the `%CLINK_COMPLETIONS_DIR%` environment variable (multiple directories may be separated by semicolons).
2. If the [`clink.path`](#clink_dot_path) setting is set, then:
   - A `completions` subdirectory under each directory in the setting (multiple directories may be separated by semicolons).
3. If the `clink.path` setting is not set, then:
   - If the Clink program directory has a `completions` subdirectory, it is searched next (e.g. `C:\tools\clink\completions`).
   - If the Clink profile directory has a `completions` subdirectory, it is searched next (e.g. `C:\Users\me\AppData\Local\clink\completions`).
4. A `completions` subdirectory under each directory listed in the `%CLINK_PATH%` environment variable (multiple directories may be separated by semicolons).

> **Note:**  If a script defines more than an argmatcher, then putting it in a completions directory may cause its other functionality to not work until a command is typed with the same name as the script.  For example, if a script in a completions directory defines an argmatcher and also a prompt filter, the prompt filter won't be loaded until the corresponding command name is typed.  Whether that is desirable depends on the script and on your preference.

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

Clink provides a framework for writing complex argument match generators in Lua.  It works by creating a parser object that describes a command's arguments and flags and associating the parser with one or more commands.  When Clink detects a parser is associated with the command being edited, it uses the parser to generate matches.

Here is an example of a simple parser for the command `foobar`;

```lua
clink.argmatcher("foobar")
:addflags("-foo", "-bar")          -- Flags.
:addarg({ "hello", "hi" })         -- Completions for arg #1.
:addarg({ "world", "wombles" })    -- Completions for arg #2.
```

This parser describes a command that has two positional arguments each with two possible completions. It also has two flags which the parser considers to be position independent, meaning that provided the word being completed starts with the prefix character (in this example `-`) then the parser will attempt to match the word from the set of flags.

On the command line completion would look something like this:

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black;color:#cccccc">C:\&gt;foobar hello -foo wo
wombles  wonder  world
C:\&gt;foobar hello -foo wo<span style="color:#ffffff">_</span>
</code></pre>

When displaying possible completions, flag matches are only shown if the flag character has been input (so `command ` and <kbd>Alt</kbd>-<kbd>=</kbd> would list only non-flag matches, or `command -` and <kbd>Alt</kbd>-<kbd>=</kbd> would list only flag matches).

If a command doesn't have an argmatcher but is a doskey macro, Clink automatically expands the doskey macro and looks for an argmatcher for the expanded command.  A macro like `gco=git checkout $*` automatically reuses a `git` argmatcher and produces completions for its `checkout` argument.  However, it only expands the doskey macro up to the first `$`, so complex aliases like `foo=app 2$gnul text $*` or `foo=$2 $1` might behave strangely.

### Automatic Filename Completion

A fresh, empty argmatcher provides no completions.

```lua
clink.argmatcher("foobar")         -- The "foobar" command provides no completions.
```

Once any flags or argument positions have been added to an argmatcher, then the argmatcher will provide completions.

```lua
clink.argmatcher("foobar")
:addflags("-foo", "-bar")          -- Flags.
:addarg({ "hello", "hi" })         -- Completions for arg #1.
:addarg({ "world", "wombles" })    -- Completions for arg #2.
```

When completing a word that doesn't have a corresponding argument position the argmatcher will automatically use filename completion.  For example, the `foobar` argmatcher has two argument positions, and completing a third word uses filename completion.

<pre style="border-radius:initial;border:initial"><code class="plaintext" style="background-color:black;color:#cccccc">C:\&gt;foobar hello world pro
Program Files\  Program Files(x86)\  ProgramData\
C:\&gt;foobar hello world pro<span style="color:#ffffff">_</span>
</code></pre>

Use [_argmatcher:nofiles()](#_argmatcher:nofiles) if you want to disable the automatic filename completion and "dead end" an argmatcher for extra words.

```lua
clink.argmatcher("foobar")
:addflags("-foo", "-bar")          -- Flags
:addarg({ "hello", "hi" })         -- Completions for arg #1
:addarg({ "world", "wombles" })    -- Completions for arg #2
:nofiles()                         -- Using :nofiles() prevents further completions.
```

### Descriptions for Flags and Arguments

Flags and arguments may optionally have descriptions associated with them.  The descriptions, if any, are displayed when listing possible completions.

Use [_argmatcher:adddescriptions()](#_argmatcher:adddescriptions) to add descriptions for flags and/or arguments.  Refer to its documentation for further details about how to use it, including how to also show arguments that a flag accepts.

For example, with the following matcher, typing `foo -`<kbd>Alt</kbd>-<kbd>=</kbd> will list all of the flags, plus descriptions for each.

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
local a_parser = clink.argmatcher():addarg({ "foo", "bar" })
local b_parser = clink.argmatcher():addarg({ "abc", "123" })
local c_parser = clink.argmatcher()
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

The function is called each time matches are generated for the argument position.

```lua
local function rainbow_function(word)
    return { "red", "white", "blue" }
end

local the_parser = clink.argmatcher()
the_parser:addarg({ "zippy", "bungle", "george" })
the_parser:addarg({ rainbow_function, "yellow", "green" })
```

The functions are passed five arguments, and should return a table of potential matches.

- `word` is a partial string for the word under the cursor, corresponding to the argument for which matches are being generated:  it is an empty string, or if a filename is being entered then it will be the path portion (e.g. for "dir1\dir2\pre" `word` will be "dir1\dir2\").
- `word_index` is the word index in `line_state`, corresponding to the argument for which matches are being generated.
- `line_state` is a [line_state](#line_state) object that contains the words for the associated command line.
- `match_builder` is a [builder](#builder) object (but for adding matches the function should return them in a table).
- `user_data` is a table that the argmatcher can use to help it parse the input line.  See [Responding to Arguments in Argmatchers](#responsive-argmatchers) for more information about the `user_data` table.

> **Compatibility Note:** When a function argument uses the old v0.4.9 `clink.match_display_filter` approach, then the `word` argument will be the full word under the cursor, for compatibility with the v0.4.9 API.

Some built-in matcher functions are available:

Function | Description
:-: | ---
[clink.dirmatches](#clink.dirmatches) | Generates directory matches.
[clink.filematches](#clink.filematches) | Generates file matches.

#### Generate Matches From History

An argument position can collect matches from the history file.  When an argument table contains `fromhistory=true` then additional matches are generated by parsing the history file to find values for that argument slot from commands in the history file.

This example generates matches for arguments to a `--host` flag by parsing the history file for host names used in the past, and also includes the current computer name.

```lua
local host_parser = clink.argmatcher():addarg({ fromhistory=true, os.getenv("COMPUTERNAME") })
clink.argmatcher("program"):addflags({ "--host"..host_parser })
```

#### Disable Sorting Matches

Match completions are normally listed in sorted order.  In some cases it may be desirable to disable sorting and list match completions in a specific order.  To disable sorting, include `nosort=true` in the argument table.  When sorting is disabled, matches are listed in the order they were added.

```lua
local the_parser = clink.argmatcher()
the_parser:addarg({ nosort=true, "red", "orange", "yellow", "green", "blue", "indigo", "violet" })
```

#### Adaptive Argmatchers

Some argmatchers may need to adapt on the fly.  For example, a program may have different features available depending on the current directory, or may want to define its arguments and flags by parsing the `--help` text from running a program.

An argmatcher can define a "delayed initialization" callback function that gets calls when the argmatcher gets used, allowing it to defer potentially expensive initialization work until it's actually needed.  An argmatcher can also define a separate "delayed initialization" function for each argument position.

##### Delayed initialization for the argmatcher

You can use [_argmatcher:setdelayinit()](#_argmatcher:setdelayinit) to set a function that performs delayed initialization for the argmatcher.  The function receives one parameter:

- `argmatcher` is the argmatcher to be initialized.

If the definition needs to adapt based on the current directory or other criteria, then the callback function should first test whether the definition needs to change.  If so, first reset the argmatcher and then initialize it.  To reset the argmatcher, use [_argmatcher:reset()](#_argmatcher:reset) which resets it back to an empty, freshly created state.

```lua
local prev_dir = ""

-- Initialize the argmatcher.
-- v1.3.12 and higher receive a command_word parameter as well, which is the
-- word in the command line that matched this argmatcher.
local function init(argmatcher, command_word)
    local r = io.popen("some_command --help 2>nul")
    for line in r:lines() do
        -- PUT PARSING CODE HERE.
        -- Use the Lua string functions to parse the lines.
        -- Use argmatcher:addflags(), argmatcher:addarg(), etc to initialize the argmatcher.
    end
    r:close()
end

-- This function has the opportunity to reset and (re)initialize the argmatcher.
local function ondelayinit(argmatcher)
    local dir = os.getcwd()
    if prev_dir ~= dir then  -- When current directory has changed,
        prev_dir = dir      -- Remember the new current directory,
        argmatcher:reset()  -- Reset the argmatcher,
        init(argmatcher)    -- And re-initialize it.
    end
end

-- Create the argmatcher and set up delayed initialization.
local m = clink.argmatcher("some_command")
if m.setdelayinit then        -- Can't use setdelayinit before Clink v1.3.10.
    m:setdelayinit(ondelayinit)
end
```

##### Delayed initialization for an argument position

If the overall flags and meaning of the argument positions don't need to be updated, and only the possible values need to be updated within certain argument positions, then you can include <code>delayinit=<span class="arg">function</span></code> in the argument table.

The <span class="arg">function</span> should return a table of matches which will be added to the values for the argument position.  The table of matches supports the same syntax as [_argmatcher:addarg()](#_argmatcher:addarg).  The function receives two parameters:

- `argmatcher` is the current argmatcher.
- `argindex` is the argument position in the argmatcher (0 is flags, 1 is the first argument position, 2 is the second argument position, and so on).

The <span class="arg">function</span> is called only once, the first time the argument position is used.  The only way for the function to be called again for that argmatcher is to use [Delayed initialization for the argmatcher](#delayed-initialization-for-the-argmatcher) and reset the argmatcher and then re-initialize it.

Delayed initialization for an argument position is different from [Functions As Argument Options](#functions-as-argument-options).  The `delayinit` function is called the first time the argmatcher is used, and the results are added to the matches for the rest of the Clink session.  But a function as an argument option is called every time matches are generated for the argument position, and it is never called when applying input line coloring.

```lua
-- A function to delay-initialize argument values.
-- This function is used to delay-initialize two different argument positions,
-- and so it gets called up to two separate times (once for each position where
-- it is specified).
-- If the function needs to behave slightly differently for different
-- argmatchers or argument positions, it can use the two parameters it receives
-- to identify the specific context in which it is being called.
local function sc_init_dirs(argmatcher, argindex)
    return {
        path.join(os.getenv("USERPROFILE"), "Documents"),
        path.join(os.getenv("USERPROFILE"), "Pictures")
    }
end

-- A function to delay-initialize flag values.
local function sc_init_flags(argmatcher)
    -- This calls sc_init_dirs() when the '--dir=' flag is used.
    return { "--dir=" .. clink.argmatcher():addarg({ delayinit=sc_init_dirs }) }
end

-- Define an argmatcher with two argument positions, and the second one uses
-- delayed initialization.
local m = clink.argmatcher("some_command")
m:addarg({ "abc", "def", "ghi" })
m:addarg({ delayinit=sc_init_dirs }) -- This sc_init_dirs() when the second arg position is used.

-- You can also use delayinit with flags, but you must set the flag prefix
-- character(s) so that Clink can know when to call the delayinit function.
m:addflags({ delayinit=sc_init_flags })
m:setflagprefix("-")
```

<a name="responsive-argmatchers"></a>

#### Responding to Arguments in Argmatchers

Argmatchers can be more involved in parsing the command line, if they wish.

An argmatcher can supply an "on arg" function to be called when the argmatcher parses an argument position.  The function can influence parsing the rest of the input line.  For example, the presence of a flag `--only-dirs` might change what match completions should be provided somewhere else in the input line.

Supply an "on arg" function by including <code>onarg=<span class="arg">function</span></code> in the argument table with [_argmatcher:addarg()](#_argmatcher:addarg).  The function is passed five arguments, and it doesn't return anything (the function receives the same arguments as [Functions As Argument Options](#functions-as-argument-options)).

- `word` is a partial string for the word under the cursor, corresponding to the argument for which matches are being generated:  it is an empty string, or if a filename is being entered then it will be the path portion (e.g. for "dir1\dir2\pre" `word` will be "dir1\dir2\").
- `word_index` is the word index in `line_state`, corresponding to the argument for which matches are being generated.
- `line_state` is a [line_state](#line_state) object that contains the words for the associated command line.
- `match_builder` is a [builder](#builder) object (but for adding matches the function should return them in a table).
- `user_data` is a table that the argmatcher can use to help it parse the input line.

When parsing begins, the `user_data` is an empty table.  Each time a flag or argument links to another argmatcher, the new argmatcher gets a separate new empty `user_data` table.  Your "on arg" functions can set data from the table, and then can also get data that was set earlier by your "on arg" functions.

An "on arg" function can also use [os.chdir()](#os.chdir) to set the current directory.  Generating match completions saves and restores the current directory when finished, so argmatcher "on arg" functions can set the current directory and thus cause match completion later in the input line to complete file names relative to the change directory.  For example, the built-in `cd` and `pushd` argmatches use an "on arg" function so that `pushd \other_dir & program `<kbd>Tab</kbd> can complete file names from `\other_dir` instead of the (real) current directory.

```lua
local function onarg_pushd(arg_index, word, word_index, line_state)
    -- Match generation after this is relative to the new directory.
    if word ~= "" then
        os.chdir(word)
    end
end

clink.argmatcher("pushd")
:addarg({
    onarg=onarg_pushd,  -- Chdir to the directory argument.
    clink.dirmatches,   -- Generate directory matches.
})
:nofiles()
```

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

Turn on [automatic suggestions](#autosuggest_enable) with `clink set autosuggest.enable true`.  Once enabled, Clink will show suggestions in a [muted color](#color.suggestion) after the end of the typed command.  Accept the whole suggestion with the <kbd>Right</kbd> arrow or <kbd>End</kbd> key, accept the next word of the suggestion with <kbd>Ctrl</kbd>-<kbd>Right</kbd>, or accept the next full word of the suggestion up to a space with <kbd>Shift</kbd>-<kbd>Right</kbd>.  You can ignore the suggestion if it isn't what you want; suggestions have no effect unless you accept them first.

Scripts can provide custom suggestion generators, in addition to the built-in options:
1. Create a new suggestion generator by calling [clink.suggester()](#clink.suggester) along with a name that identifies the suggestion generator, and can be added to the [`autosuggest.strategy`](#autosuggest_strategy) setting.
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
<tr class="lmtr"><td class="lmtd"><a href="#terminal-support">Terminal Support</a></td><td class="lmtd">Information about how Clink's terminal support works.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#troubleshooting-tips">Troubleshooting Tips</a></td><td class="lmtd">How to troubleshoot and report problems.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#privacy">Privacy</a></td><td class="lmtd">Privacy statement for Clink.</td></tr>
</table>

<a name="keybindings"></a>

## Customizing Key Bindings

Key bindings are defined in .inputrc files.

The `clink-show-help` command is bound to <kbd>Alt</kbd>-<kbd>H</kbd> and lists all currently active key bindings.  The list displays "friendly" key names, and these names are generally not suitable for use in .inputrc files.  For example "Up" is the friendly name for `"\e[A"`, and "A-C-F2" is the friendly name for `"\e\e[1;5Q"`.  To see key sequence strings suitable for use in .inputrc files use `clink echo` as described below.

<table class="linkmenu">
<tr class="lmtr"><td class="lmtd"><a href="#the-inputrc-file">The .inputrc file</a></td><td class="lmtd">Where to find the .inputrc file, and more information about it.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#discoverkeysequences">Discovering Clink key sequences</a></td><td class="lmtd">How to find key names to use for key bindings.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#specialkeys">Binding special keys</a></td><td class="lmtd">A table of special key names.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#luakeybindings">Lua key bindings</a></td><td class="lmtd">How to bind keys to Lua functions.</td></tr>
<tr class="lmtr"><td class="lmtd"><a href="#nometakey">I do not have a Meta or <kbd>Alt</kbd> key</a></td><td class="lmtd">What to do if your keyboard doesn't have any <kbd>Alt</kbd> or Meta keys.</td></tr>
</table>

### The .inputrc file

You can use `clink info` to find the directories and configuration files for the current Clink session, including where the .inputrc file is located, or can be located.  See the [Readline Init File](#init-file) section for detailed information about .inputrc files.

> **Note:** Third party console hosts such as ConEmu may have their own key bindings that supersede Clink.  They usually have documentation for how to change or disable their key bindings to allow console programs to handle the keys instead.

<a name="discoverkeysequences"></a>

### Discovering Clink key sequences

Clink provides an easy way to find the key sequence for any key combination that Clink supports. Run `clink echo` and then press key combinations; the associated key binding sequence is printed to the console output and can be used for a key binding in the inputrc file.

A chord can be formed by concatenating multiple key binding sequences. For example, `"\C-X"` and `"\e[H"` can be concatenated to form `"\C-X\e[H"` representing the chord <kbd>Ctrl</kbd>-<kbd>X</kbd>,<kbd>Home</kbd>.

When finished, press <kbd>Ctrl</kbd>-<kbd>C</kbd> to exit from `clink echo`.

> **Note:** With non-US keyboard layouts, `clink echo` is not able to ignore dead key input (accent keys, for example).  It prints the key sequence for the dead key itself, which is not useful.  You can ignore that and press the next key, and then it prints the correct key sequence to use in key bindings.

<a name="specialkeys"></a>

### Binding special keys

Here is a table of the key binding sequences for the special keys.  Clink primarily uses VT220 emulation for keyboard input, but also uses some Xterm extended key sequences.

|           |Normal     |Shift        |Ctrl         |Ctrl-Shift   |Alt       |Alt-Shift   |Alt-Ctrl     |Alt-Ctrl-Shift|
|:-:        |:-:        |:-:          |:-:          |:-:          |:-:       |:-:         |:-:          |:-:           |
|Up         |`\e[A`     |`\e[1;2A`    |`\e[1;5A`    |`\e[1;6A`    |`\e[1;3A` |`\e[1;4A`   |`\e[1;7A`    |`\e[1;8A`     |
|Down       |`\e[B`     |`\e[1;2B`    |`\e[1;5B`    |`\e[1;6B`    |`\e[1;3B` |`\e[1;4B`   |`\e[1;7B`    |`\e[1;8B`     |
|Left       |`\e[D`     |`\e[1;2D`    |`\e[1;5D`    |`\e[1;6D`    |`\e[1;3D` |`\e[1;4D`   |`\e[1;7D`    |`\e[1;8D`     |
|Right      |`\e[C`     |`\e[1;2C`    |`\e[1;5C`    |`\e[1;6C`    |`\e[1;3C` |`\e[1;4C`   |`\e[1;7C`    |`\e[1;8C`     |
|Insert     |`\e[2~`    |`\e[2;2~`    |`\e[2;5~`    |`\e[2;6~`    |`\e[2;3~` |`\e[2;4~`   |`\e[2;7~`    |`\e[2;8~`     |
|Delete     |`\e[3~`    |`\e[3;2~`    |`\e[3;5~`    |`\e[3;6~`    |`\e[3;3~` |`\e[3;4~`   |`\e[3;7~`    |`\e[3;8~`     |
|Home       |`\e[H`     |`\e[1;2H`    |`\e[1;5H`    |`\e[1;6H`    |`\e[1;3H` |`\e[1;4H`   |`\e[1;7H`    |`\e[1;8H`     |
|End        |`\e[F`     |`\e[1;2F`    |`\e[1;5F`    |`\e[1;6F`    |`\e[1;3F` |`\e[1;4F`   |`\e[1;7F`    |`\e[1;8F`     |
|PgUp       |`\e[5~`    |`\e[5;2~`    |`\e[5;5~`    |`\e[5;6~`    |`\e[5;3~` |`\e[5;4~`   |`\e[5;7~`    |`\e[5;8~`     |
|PgDn       |`\e[6~`    |`\e[6;2~`    |`\e[6;5~`    |`\e[6;6~`    |`\e[6;3~` |`\e[6;4~`   |`\e[6;7~`    |`\e[6;8~`     |
|Tab        |`\t`       |`\e[Z`       |`\e[27;5;9~` |`\e[27;6;9~` | -        | -          | -           | -            |
|Space      |`Space`    |`\e[27;2;32~`|`\e[27;5;32~`|`\e[27;6;32~`| -        | -          |`\e[27;7;32~`|`\e[27;8;32~` |
|Backspace  |`^h`       |`\e[27;2;8~` |`Rubout`     |`\e[27;6;8~` |`\e^h`    |`\e[27;4;8~`|`\eRubout`   |`\e[27;8;8~`  |
|F1         |`\eOP`     |`\e[1;2P`    |`\e[1;5P`    |`\e[1;6P`    |`\e\eOP`  |`\e\e[1;2P` |`\e\e[1;5P`  |`\e\e[1;6P`   |
|F2         |`\eOQ`     |`\e[1;2Q`    |`\e[1;5Q`    |`\e[1;6Q`    |`\e\eOQ`  |`\e\e[1;2Q` |`\e\e[1;5Q`  |`\e\e[1;6Q`   |
|F3         |`\eOR`     |`\e[1;2R`    |`\e[1;5R`    |`\e[1;6R`    |`\e\eOR`  |`\e\e[1;2R` |`\e\e[1;5R`  |`\e\e[1;6R`   |
|F4         |`\eOS`     |`\e[1;2S`    |`\e[1;5S`    |`\e[1;6S`    |`\e\eOS`  |`\e\e[1;2S` |`\e\e[1;5S`  |`\e\e[1;6S`   |
|F5         |`\e[15~`   |`\e[15;2~`   |`\e[15;5~`   |`\e[15;6~`   |`\e\e[15~`|`\e\e[15;2~`|`\e\e[15;5~` |`\e\e[15;6~`  |
|F6         |`\e[17~`   |`\e[17;2~`   |`\e[17;5~`   |`\e[17;6~`   |`\e\e[17~`|`\e\e[17;2~`|`\e\e[17;5~` |`\e\e[17;6~`  |
|F7         |`\e[18~`   |`\e[18;2~`   |`\e[18;5~`   |`\e[18;6~`   |`\e\e[18~`|`\e\e[18;2~`|`\e\e[18;5~` |`\e\e[18;6~`  |
|F8         |`\e[19~`   |`\e[19;2~`   |`\e[19;5~`   |`\e[19;6~`   |`\e\e[19~`|`\e\e[19;2~`|`\e\e[19;5~` |`\e\e[19;6~`  |
|F9         |`\e[20~`   |`\e[20;2~`   |`\e[20;5~`   |`\e[20;6~`   |`\e\e[20~`|`\e\e[20;2~`|`\e\e[20;5~` |`\e\e[20;6~`  |
|F10        |`\e[21~`   |`\e[21;2~`   |`\e[21;5~`   |`\e[21;6~`   |`\e\e[21~`|`\e\e[21;2~`|`\e\e[21;5~` |`\e\e[21;6~`  |
|F11        |`\e[23~`   |`\e[23;2~`   |`\e[23;5~`   |`\e[23;6~`   |`\e\e[23~`|`\e\e[23;2~`|`\e\e[23;5~` |`\e\e[23;6~`  |
|F12        |`\e[24~`   |`\e[24;2~`   |`\e[24;5~`   |`\e[24;6~`   |`\e\e[24~`|`\e\e[24;2~`|`\e\e[24;5~` |`\e\e[24;6~`  |

When the `terminal.differentiate_keys` setting is enabled then the following key bindings are also available:

|    |Ctrl           |Ctrl-Shift     |Alt            |Alt-Shift      |Alt-Ctrl       |Alt-Ctrl-Shift |
|:-: |:-:            |:-:            |:-:            |:-:            |:-:            |:-:            |
|`H` |`\e[27;5;72~`  |`\e[27;6;72~`  |`\eh`          |`\eH`          |`\e[27;7;72~`  |`\e[27;8;72~`  |
|`I` |`\e[27;5;73~`  |`\e[27;6;73~`  |`\ei`          |`\eI`          |`\e[27;7;73~`  |`\e[27;8;73~`  |
|`M` |`\e[27;5;77~`  |`\e[27;6;77~`  |`\em`          |`\eM`          |`\e[27;7;77~`  |`\e[27;8;77~`  |
|`[` |`\e[27;5;219~` |`\e[27;6;219~` |`\e[27;3;219~` |`\e[27;4;219~` |`\e[27;7;219~` |`\e[27;8;219~` |

The <a href="#terminal_raw_esc">`terminal.raw_esc`</a> setting controls the binding sequence for the <kbd>Esc</kbd> key and a couple of other keys:

|`terminal.raw_esc` Setting Value|Esc|Alt-[|Alt-Shift-O|
|:-|-|-|-|
|False (the default)|`\e[27;27~`|`\e[27;3;91~`|`\e[27;4;79~`
|True (replicate Unix terminal input quirks and issues)|`\e`|`\e[`|`\eO`

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

Here is an example that makes <kbd>F7</kbd>/<kbd>F8</kbd> jump to the previous/next screen line containing "error" or "warn" colored red or yellow, and makes <kbd>Shift</kbd>-<kbd>F7</kbd>/<kbd>Shift</kbd>-<kbd>F8</kbd> jump to the previous/next prompt line.

One way to use these is when reviewing compiler errors after building a project at the command line.  Press <kbd>Shift</kbd>-<kbd>F7</kbd> to jump to the previous prompt line, and then use <kbd>F8</kbd> repeatedly to jump to each next compiler warning or error listed on the screen.

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

<a name="nometakey"></a>

### I do not have a Meta or <kbd>Alt</kbd> key

If you do not have a Meta or <kbd>Alt</kbd> key, or another key working as a Meta key, there is another way to generate "metafied" keystrokes such as <kbd>M-k</kbd>.

You can configure `clink set terminal.raw_esc true` to make the <kbd>Esc</kbd> work as it does in Unix and Linux, and then you can type <kbd>Esc</kbd> followed by <kbd>k</kbd>. This is known as "metafying" the <kbd>k</kbd> key.

Clink is a Windows program, and so by default it makes the <kbd>Esc</kbd> key reset the input state, since that's how <kbd>Esc</kbd> generally works on Windows. When you enable the <a href="#terminal_raw_esc">`terminal.raw_esc`</a> Clink setting, the <kbd>Esc</kbd> key changes its behavior, and pressing unbound special keys can land you in all of the same strange "stuck input" situations as in Unix and Linux.

But it might be more convenient to acquire a keyboard that has an <kbd>Alt</kbd> key.

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

There are several settings that control how history works.  Run `clink set history*` to see them.

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

### History timestamps

History items can optionally save the timestamp when they were added, and the timestamps can be shown in the `history` command.

Use `clink set history.time_stamp off` to not save or show timestamps for history items (this is the default).  Turning off timestamps doesn't remove existing timestamps.

Use `clink set history.time_stamp save` to save timestamps for each history item but only show them in the `history` command when the `--show-time` flag is used.

Use `clink set history.time_stamp show` to save timestamps for each history item and show them in the `history` command unless the `--bare` flag is used.

Use <code>clink set history.time_format <span class="arg">format</span></code> to specify the format for showing timestamps (the default format is <code>%F %T &nbsp</code>).

The <span class="arg">format</span> string may contain regular characters and special format specifiers.  Format specifiers begin with a percent sign (`%`), and are expanded to their corresponding values.  For a list of possible format specifiers, refer to the C++ strftime() documentation.

Some common format specifiers are:

Specifier | Expands To
-|-
`%a` | Abbreviated weekday name for the locale (e.g. Thu).
`%b` | Abbreviated month name for the locale (e.g. Aug).
`%c` | Date and time representation for the locale.
`%D` | Short MM/DD/YY date (e.g. 08/23/01).
`%F` | Short YYYY/MM/DD date (e.g. 2001-08-23).
`%H` | Hour in 24-hour format (00 - 23).
`%I` | Hour in 12-hour format (01 - 12).
`%m` | Month (01 - 12).
`%M` | Minutes (00 - 59).
`%p` | AM or PM indicator for the locale.
`%r` | 12-hour clock time for the locale (e.g. 02:55:41 pm).
`%R` | 24-hour clock time (e.g. 14:55).
`%S` | Seconds (00 - 59).
`%T` | ISO 8601 time format HH:MM:SS (e.g. 14:55:41).
`%x` | Date representation for the locale.
`%X` | Time representation for the locale.
`%y` | Year without century (00 - 99).
`%Y` | Year with century (e.g. 2001).
`%%` | A `%` sign.

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

## Terminal Support

Windows programs generally don't need to worry about terminal support.  But the Readline library used by Clink comes from Unix, where there are many different kinds of terminals, and the library requires certain kinds of terminal support.

Clink's keyboard driver generally produces VT220 style key sequences, but it also includes many extensions from Xterm and other sources.  Use `clink echo` to find key sequences for specific inputs.

Clink's terminal output driver is designed for use with Windows and its console subsystem.  Clink can optionally handle output itself instead, and emulate terminal output support when the `terminal.emulation` setting is `emulate`, or when `auto` and Clink is running on an older version of Windows that doesn't support ANSI escape codes.  In emulation mode, 8 bit and 24 bit color escape codes are mapped to the nearest 4 bit colors.

By default Clink sets the cursor style to a blinking horizontal partial-height block, or to a blink full-height solid block.  Some terminals support escape codes to select alternative cursor styles.  Clink provides environment variables where you may optionally provide escape codes to override the cursor style.  `%CLINK_TERM_VE%` selects the style for the normal cursor (insert mode), `%CLINK_TERM_VS%` selects the style for the enhanced cursor (overwrite mode).

Special codes recognized in the cursor style escape code strings:

<table>
<tr><th>Code</th><th>Description</th></tr>
<tr><td><code>\e</code></td><td>Translated to the ESC character (27 decimal, 0x1b hex).</td></tr>
<tr><td><code>\x<span class="arg">HH</span></code></td><td>Translated to the character matching the hex <span class="arg">HH</span> value.<br/>E.g. <code>\x1b</code> is the same as <code>\e</code>, or <code>\x08</code> is a backspace, etc.</td></tr>
<tr><td><code>\\</code></td><td>Translated to the <code>\</code> character.</td></tr>
<tr><td><code><code>\<span class="arg">c</span></code></td><td>Any other backslash is translate to whatever character immediately follows it.<br/>E.g. <code>\a</code> becomes <code>a</code>.</td></tr>
</table>

Refer to the documentation for individual terminal programs to find what (if any) escape codes they may support.  The default console in Windows 10 supports the [DECSCUSR](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h4-Functions-using-CSI-_-ordered-by-the-final-character-lparen-s-rparen:CSI-Ps-SP-q.1D81) escape codes for selecting cursor shape.

This .cmd script sets the normal cursor to a blinking vertical bar, and the enhanced cursor to a non-blinking solid box:

```cmd
set CLINK_TERM_VE=\e[5 q
set CLINK_TERM_VS=\e[2 q
```

Or this .cmd script sets the normal cursor to blink, and the enhanced cursor to not blink:

```cmd
set CLINK_TERM_VE=\e[?12h
set CLINK_TERM_VS=\e[?12l
```

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
