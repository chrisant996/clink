ChrisAnt Plans

<br>

# Bugs
- `%` doesn't get quoted in filenames, as a side effect of how quoting was avoided for env vars.

# Documentation
- Describe the new argmatcher/etc syntax.
- Supply sample inputrc file(s).
- Explain from where scripts are loaded (mridgers #519 has some info on that).
- Document Readline color configuration -- LS_COLORS and also Clink settings such as `colour.readonly`.

<br>
<br>

# Phase 1
The Phase 1 goal is to have a working version that for the most part meets or exceeds Clink 0.4.8 stability and functionality.

## Bugs
- `update_internal()` seems like it's getting called once per char in a key sequence; probably only needs to happen after a key that finished a key binding.
- Should only fold path separators in pathish matches.

## LUA
Lua support changed significantly.  Explore how to support backward compatibility for existing scripts [#6](https://github.com/chrisant996/clink/issues/6).
- **Prompt filter:**  seems to be working now.
- **Generator:**  generator syntax is backward compatible by making the first argument once again be the end word; see commit f9c647b965 for the breaking syntax change, and commit 265ac265dc for the backward compatibility change.
- **Argmatcher:**  problems...
  - `dotnet.lua:38`: attempt to call method 'add_flags' (a nil value)
    - `register_parser(cmd, parser({...}))` => The extra `parser` layering goes haywire.
    - `build_parser:add_flags(...)` => Not adding `...` as flags.
  - `net.lua:31`: attempt to call method 'flatten_argument' (a nil value)
  - `pip.lua:121`: attempt to call method 'add_flags' (a nil value)
  - `pipenv.lua:54`: attempt to call method 'add_flags' (a nil value)
  - `scoop.lua:296`: attempt to call method 'set_flags' (a nil value)
  - `vagrant.lua:137`: attempt to call method 'flatten_argument' (a nil value)
- There might still be other syntax incompatibilities...?

## Features

### Other
- `match.ignore_case` can't be working correctly, and probably readline settings should determine it.  _(Update: oh, it's obsolete now that tab_completer is gone.)_
- `_rl_completion_case_map` isn't supported properly in clink lua APIs, nor in general.  _(The 0.4.8 implementation simply converted `-` and `_` to `?` and accepted all matches -- although maybe it filtered the results afterwards?)_
- When convert-meta is off, then when binding `\M-h` (etc) the key name gets interpreted differently than Clink expects.
- Promote store_impl to be a template and public, so the popup list can use it?
- Popup list:
  - What to do about completion colors?
  - Make it owner draw and add text like "dir", "alias", etc?
  - Add stat chars when so configured?
  - Ability to delete, rearrange, and edit popup list items?
  - Show the current incremental search string somewhere?
- Use `npm` to run `highlight.js` at compile time?
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width makes master go haywire.  Probably more ecma48 terminal issues.  Probably related to commit 8aeaa14.
- Use `path::normalise` to clean up input like "\wbin\\\\cli" when using `complete` and `menu-complete`.
- Symlink support (displaying matches, and whether to append a path separator).
- Looks like suppress_char_append(), suppress_quoting(), and slash_translation() are all dead code?
- 32a672e is suspicious:  "Resend ESC when it gets dropped by incremental searching".

## Questions
- What is `set-mark`?
- How does `reverse-search-history` work?
- How does `kill-line` work?
- What to do about `MODE4` in the code base?

<br>
<br>

# Phase 2
The Phase 2 goal is to produce a viable Beta Release with broader compatibility in place, and some new features added.

> I really want CUA Selection support!  It seems either extremely invasive in Readline, or very invasive + uses a custom rendering routine.  Not sure yet the best way to proceed.

## Problems
- Lua `globfiles` and `globdirs` should return whether the files and dirs are hidden, to save _N_ additional calls to look up the hidden attributes.
- Dissatisfied with the match pipeline:  typing `exit` and hitting **Enter** triggers the match pipeline.
  - It seems more efficient to not invoke it until `complete` or `menu-complete` (etc).
  - But eventually in order to color arguments/etc the match pipeline will need to run while typing, so maybe leave it be.
- Use [Microsoft Detours](https://github.com/microsoft/detours) instead of the current implementation in clink?
- Check if there's a newer update to the `wcwidth` implementation.

## Features

### Commands
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.
- A way to disable/enable clink once injected.
- A way to disable/enable prompt filtering once injected.
- Allow to search the console output (not command history) with a RegExp [#166](https://github.com/mridgers/clink/issues/166).
  - Ideally enable lua to do searches, set scroll position, retrieve text from the screen buffer, and possibly even modify the editing line.  Imagine being able to bind a key to a lua script to search for next/prev line with red or yellow colored text, or to search for "error:", or etc.  Think of the possibilities!

## Issues Backlog [clink/issues](https://github.com/mridgers/clink/issues)
- [#542](https://github.com/mridgers/clink/issues/542) VS Code not capturing std output
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [#398](https://github.com/mridgers/clink/issues/398) Cmd gets unresponsive after "set /p" command.

<br>
<br>

# EVENTUALLY
More ambitious things like CUA Selection and coloring arguments and flags while editing (according to lua argmatchers).

## Input
- Try to make unbound keys like **Shift-Left** tell conhost that they haven't been handled, so conhost can do its fancy CUA marking.
  - Mysteriously, holding down a bound key like **Ctrl+Up** or **Ctrl+A** was sometimes letting conhost periodically intercept some of the keypresses!  That shouldn't be possible, but maybe there's a way to deterministically cause that behavior?
  - **Ctrl+M** to activate marking mode.
  - Shouldn't be possible, but at one point **Ctrl+A** was sometimes able to get interpreted as Select All, and **Shift+Up** was sometimes able to get interpreted as Select Line Up.  That had to have been a bug in how clink was managing SetConsoleMode, but maybe there's a way to exploit that for good?
    - Maybe hook some API and call original ReadConsoleW, and feed it input somehow to trick the console host into doing its thing?

## Editing
- Custom color for readline input.  Might prefer to completely replace readline's line drawing, since it's trying to minimize updates over terminal emulators, and that makes it much harder to colorize the editing line (and arguments).

## Key Bindings
- **https://invisible-island.net/xterm/modified-keys.html**
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc.
- Implement modes so it can be compatible with v0.4.9 key sequences?

## Commands
- Need a way to do `menu-complete` without wrapping around.
- Marking mode in-app?  It's a kludge, but it copies with HTML formatting (and even uses the color scheme).
- **F8** should behave like `history-search-backward` but wrap around.
- Complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.

### Quoting in completions
- Support continuing completion of `"\Program Files"\`**Tab**.
- Oops, Readline walks backward to figure out quoting.  That doesn't work reliably; must walk forward from the beginning otherwise `"\Program Files"\` is treated as though the ending `"\` is starting a new filename.

## CUA EDITING
- Select all.
- **Shift+arrows** and etc do normal CUA style editing _[or maybe just let the new conhost handle that automagically?]_
- **Ctrl+C** do copy when CUA selection exists (might need to just intercept input handling, similar to how **Alt+H** was being intercepted), otherwise do Ctrl+C (Break).
- **Ctrl+X** cut selection.
- Custom color for CUA selected text.

## Issues Backlog [clink/issues](https://github.com/mridgers/clink/issues)
- [#427](https://github.com/mridgers/clink/issues/427) The history does not record the 2nd+ lines of '^' escaped lines
- [#532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines
  - It's pretty risky to just paste-and-go.
  - Maybe add an option to convert newlines into "&" instead?
  - Or maybe let readline do multiline editing and accept them all as a batch on **Enter**?
- [#396](https://github.com/mridgers/clink/issues/396) Pasting unicode emoji in a clink-enabled console _[Update:  How well does pasting emoji into bash work?  Readline might not have sufficient Unicode awareness to support emoji.]_

<br>
<br>

# FUTURE

## Fancy
- Colorize arguments recognized by lua argument parsers!  Also colorize doskey macros.
- **Async command prompt updating as a way to solve the delay in git repos.**
- **Bind keys to lua scripts?**
  - Lua scripts able to implement scrolling behavior (e.g. to scroll to next/prev compiler error, or colored text, etc).

## Readline
I've found some quirks, bugs, and performance issues in Readline.
- Log and send the changes I've made to the Readline owner, to start a conversation about possible next steps.

## Configuration

<br>
<br>

# APPENDICES

## Known Issues
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).
- [#531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There might be a way to obfuscate the fact that clink uses those, but ultimately this is kind of an inherent problem.  Getting the binaries digitally signed might be the most effective solution, but that's financially expensive.]_

## Mystery
- [#480](https://github.com/mridgers/clink/issues/480) Things don't work right when clink is in a path with spaces _[I'm not able to reproduce the problem, so dropping it off the radar for now.]_

---
Chris Antos - sparrowhawk996@gmail.com
