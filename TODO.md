ChrisAnt Plans

<br>

# Alpha Release
Some additional work is needed to get a credible alpha release ready.

## Documentation
- Describe the new argmatcher/etc syntax.

## Scripts
- Supply sample inputrc file(s).

## Commands
- Must have a way to list extended key bindings (but user-friendly key binding names can be deferred until Phase 2).

<br>
<br>

# Phase 1
The Phase 1 goal is to have a working version that for the most part meets or exceeds Clink 0.4.8 stability and functionality.

## LUA
Lua support changed significantly.  Explore how to support backward compatibility for existing scripts.
- _Done: The prompt filter now supports both 0.4.8 syntax and also the new 1.0.0 syntax._
- _Done: The argmatcher/parser syntax now has shims in place that should make it backward compatible with existing scripts (pending further testing)._
- _Done: The generator syntax is backward compatible by making the first argument once again be the end word.  See commit f9c647b965 for the breaking syntax change, and commit 265ac265dc for the backward compatibility change._
- `dotnet.lua:38`: attempt to call method 'add_flags' (a nil value)
- `net.lua:31`: attempt to call method 'flatten_argument' (a nil value)
- `pip.lua:121`: attempt to call method 'add_flags' (a nil value)
- `pipenv.lua:54`: attempt to call method 'add_flags' (a nil value)
- `scoop.lua:296`: attempt to call method 'set_flags' (a nil value)
- `vagrant.lua:137`: attempt to call method 'flatten_argument' (a nil value)
- `git_prompt.lua:83`: attempt to index field 'clink.prompt.value' (a nil value)
- There might still be other syntax incompatibilities...?

## Features

### Other
- `match.ignore_case` can't be working correctly, and probably readline settings should determine it.

## Issues Backlog [clink/issues](https://github.com/mridgers/clink/issues)
- [#502](https://github.com/mridgers/clink/issues/502) Error in folders containing [ ] characters
- [#415](https://github.com/mridgers/clink/issues/415) Different encodings in different lua functions
- [#544](https://github.com/mridgers/clink/issues/544) Clink v1.0.0.a1 doesn't support cyrillic characters keyboard input

## Questions
- What is `set-mark`?
- How does `reverse-search-history` work?
- How does `kill-line` work?

<br>
<br>

# Phase 2
The Phase 2 goal is to produce a viable Beta Release with broader compatibility in place, and some new features added.

> I really want CUA Selection support!  It seems either extremely invasive in Readline, or very invasive + uses a custom rendering routine.  Not sure yet the best way to proceed.

## Problems
- Lua `globfiles` and `globdirs` should return whether the files and dirs are hidden, to save _N_ additional calls to look up the hidden attributes.
- Have a mode that passes all ANSI escape codes through to the console host (conhost, ConEmu, etc) to allow making use of things like extended terminal codes for 256 color and 24 bit color support, etc [#487](https://github.com/mridgers/clink/issues/487).
  - Use a callback function for visible bell, rather than an ANSI escape code (which doesn't really make sense).
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width makes master go haywire.  Probably more ecma48 terminal issues.
- Over 39 thousand assertions in the unit test?!
- Use `path::normalise` to clean up input like "\wbin\\\\cli" when using `complete` and `menu-complete`.
- Dissatisfied with the match pipeline:  typing `exit` and hitting **Enter** triggers the match pipeline.
  - It seems more efficient to not invoke it until `complete` or `menu-complete` (etc).
  - But eventually in order to color arguments/etc the match pipeline will need to run while typing, so maybe leave it be.
- Use [Microsoft Detours](https://github.com/microsoft/detours) instead of the current implementation in clink?
- vi mode doesn't seem to support responding to M-C-letter or M-letter bindings, but interprets them as other things?
- vi mode doesn't seem to support `\e[27;27~` even when explicitly bound, but I don't yet understand why not.
- Toggling vi mode doesn't reload .inputrc until a new line, so $if conditional key bindings aren't active at first.
- Symlink support (displaying matches, and whether to append a path separator).
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).  There might be no way around that...

## Features

### Commands
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.
- Add a way to reset or trim the history, when there's only one (or zero) clink running [#499](https://github.com/mridgers/clink/issues/499).
- Remember previous directory, and `-` swaps back to it [#372](https://github.com/mridgers/clink/issues/372).
  - Maybe set a `CLINK_PREV_DIR` envvar, too?
  - Remember a stack of previous directories?
- A way to disable/enable clink once injected.
- A way to disable/enable prompt filtering once injected.
- Allow to search the console output (not command history) with a RegExp [#166](https://github.com/mridgers/clink/issues/166).
  - Ideally enable lua to do searches, set scroll position, retrieve text from the screen buffer, and possibly even modify the editing line.  Imagine being able to bind a key to a lua script to search for next/prev line with red or yellow colored text, or to search for "error:", or etc.  Think of the possibilities!
- Enable lua to indicate the match type (word, file, dir, link)?

### Key Bindings
- Hook up stuff via commands and/or keymaps (instead of via hard-coded custom bindings) so that everything can be remapped and reported by `show-rl-help`.
  - It may be ok for commands to select a modal custom binding map.
  - Dynamically build/augment modal binding maps based on rl bindings -- e.g. so binding **Ctrl+P** to `scroll-page-up` causes **Ctrl+P** to work in the modal binding map.
- Make `show-rl-help` able to list enhanced keys like Up, Home, Ctrl-Shift-Space, etc.
  - Translate terminal sequences into "C-A-name" in `show-rl-help` (e.g. "C-A-Up", "Ctrl-Home", "End", etc)?  But that gets weird because those aren't parseable key names.
  - Maybe have two variants of `show-rl-help` -- one that shows human readable key names, and one that shows the actual binding strings?
  - Invent an alternative syntax?
- Allow **Ctrl+M** to be discrete from **Enter**?

## Issues Backlog [clink/issues](https://github.com/mridgers/clink/issues)
- [#542](https://github.com/mridgers/clink/issues/542) VS Code not capturing std output
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [#398](https://github.com/mridgers/clink/issues/398) Cmd gets unresponsive after "set /p" command.

<br>
<br>

# EVENTUALLY
More ambitious things like CUA Selection, popup window lists for completion and history, and coloring arguments and flags while editing (according to lua argmatchers).

## Input
- Try to make unbound keys like **Shift-Left** tell conhost that they haven't been handled, so conhost can do its fancy CUA marking.
  - Mysteriously, holding down a bound key like **Ctrl+Up** or **Ctrl+A** was sometimes letting conhost periodically intercept some of the keypresses!  That shouldn't be possible, but maybe there's a way to deterministically cause that behavior?
  - **Ctrl+M** to activate marking mode.
  - Shouldn't be possible, but at one point **Ctrl+A** was sometimes able to get interpreted as Select All, and **Shift+Up** was sometimes able to get interpreted as Select Line Up.  That had to have been a bug in how clink was managing SetConsoleMode, but maybe there's a way to exploit that for good?

## Editing
- Custom color for readline input.  Might prefer to completely replace readline's line drawing, since it's trying to minimize updates over terminal emulators, and that makes it much harder to colorize the editing line (and arguments).

## Key Bindings
- **https://invisible-island.net/xterm/modified-keys.html**
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc.
- Translate terminal sequences into "C-A-S-name" in `show-rl-help`.
- Implement modes so it can be compatible with v0.4.9 key sequences?

## Commands
- Need a way to do `menu-complete` without wrapping around.
- Popup completion list.
- Popup history list.
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
- [#396](https://github.com/mridgers/clink/issues/396) Pasting unicode emoji in a clink-enabled console

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
- Trailing added backslash when listing directory matches isn't counted as part of the column width.

## Configuration

<br>
<br>

# APPENDICES

## Known Issues
- [#531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There might be a way to obfuscate the fact that clink uses those, but ultimately this is kind of an inherent problem.  Getting the binaries digitally signed might be the most effective solution, but that's financially expensive.]_

## Mystery
- [#480](https://github.com/mridgers/clink/issues/480) Things don't work right when clink is in a path with spaces _[I'm not able to reproduce the problem, so dropping it off the radar for now.]_

---
Chris Antos - sparrowhawk996@gmail.com
