ChrisAnt Plans

# Phase 1

## LUA
- Lua support changed significantly.  Explore how to support backward compatibility for existing scripts.
  - The prompt filter now supports both 0.4.8 syntax and also the new 1.0.0 syntax.
  - The argmatcher/generator syntax is different enough that it's not clear how to support both the old and new syntax concurrently.  I still hope to be able to support both, but it's probably simpler to just update existing scripts to the new API.

## Features
- Add an option for `menu-complete` to not automatically accept the completion when there's only one possibility.  Because there's no reliable visual indicator it reset when it's a directory, and because it's a significant departure from CMD muscle memory.

### Quoting in completions
Readline completion doesn't handle quotes correctly?!
- Support completing `"\Program F`**Tab**.
- Close off quotes like CMD and CASH do (`"\Program Files"\`).
- Support continuing completion of `"\Program Files"\`**Tab**.
- Oops, Readline walks backward to figure out quoting.  That doesn't work; must walk forward from the beginning otherwise `"\Program Files"\` is treated as though the ending `"\` is starting a new filename.

### Scrolling mode
- Have commands for scrolling up/down by a page or line (or top/bottom of buffer).
- The commands should each activate scrolling mode, and those same keys (and only those keys) should scroll while scrolling mode is active.
  - Might need a hook in readline to do things before a command is processed, e.g. to exit scrolling mode when any non-scrolling command is invoked.
- Because I don't want **Shift+PgUp/PgDn** hard-coded for scrolling.

### Other
- Win10 console mode flag to support ANSI sequences and colors; seems to maybe be working already?
- `completion-query-items` < 0 should use ( terminal height / 2 ) / terminal width.
- Investigate [XTerm 256 support](https://conemu.github.io/en/AnsiEscapeCodes.html) [#487](https://github.com/mridgers/clink/issues/487).
- _The new bindable **Esc** isn't yet compatible with vi mode!_
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width makes master go haywire.  Probably more ecma48 terminal issues.
- Allow conhost to handle **Shift+Left** and etc for CUA selection?
- `fnprint()` is still doing IO to figure out colors.  I think I've eliminated the rest of the stat calls, though.

## Questions
- What is `set-mark`?
- How does `reverse-search-history` work?
- How does `kill-line` work?

# Issues Backlog [clink/issues](https://github.com/mridgers/clink/issues)
- [#544](https://github.com/mridgers/clink/issues/544) Clink v1.0.0.a1 doesn't support cyrillic characters keyboard input
- [#542](https://github.com/mridgers/clink/issues/542) VS Code not capturing std output
- [#531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[or on execution, for me]_
- [#502](https://github.com/mridgers/clink/issues/502) Error in folders containing [ ] characters
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [#480](https://github.com/mridgers/clink/issues/480) Things don't work right when clink is in a path with spaces
- [x] [#453](https://github.com/mridgers/clink/issues/453) non-printable characters mess up rendering vs caret position
  - _Double check that it's fixed by the Readline update, as advertised._
- [#415](https://github.com/mridgers/clink/issues/415) Different encodings in different lua functions
- [#398](https://github.com/mridgers/clink/issues/398) Cmd gets unresponsive after "set /p" command.
- [#396](https://github.com/mridgers/clink/issues/396) Pasting unicode emoji in a clink-enabled console
- [#30](https://github.com/mridgers/clink/issues/30) wildcards not expanded.

# Phase 2

## Problems
- Over 39 thousand assertions in the unit test?!
- Use `path::normalise` to clean up input like "\wbin\\\\cli" when using `complete` and `menu-complete`.

## Key Bindings
- Hook up stuff via commands instead of via hard-coded custom bindings, so that everything can be remapped and reported by `show-rl-help`.
  - I think clink has its own key binding manager because readline doesn't have a way to select a user-defined keymap, and because readline commands are global and aren't very OOP-friendly.
  - Instead of having special hard-coded keys, use rl commands for all "top level" bindings -- only use clink bind and bind_resolver for modal keymaps while doing nested input inside some other command?
  - For modal keymaps, go ahead and use the clink binder, but dynamically build/augment the modal keymap from rl bindings (so that binding **Ctrl+P** to `scroll-page-up` uses a modal keymap where **Ctrl+P** is the pageup key).
- I want `show-rl-help` to be able to list enhanced keys like Up, Home, Ctrl-Shift-Space, etc.
  - Translate terminal sequences into "C-A-name" in `show-rl-help` (e.g. "C-A-Up", "Ctrl-Home", "End", etc)?  But that gets weird because those aren't parseable key names.
  - Maybe have two variants of `show-rl-help` -- one that shows human readable key names, and one that shows the actual binding strings?
  - Invent an alternative syntax?
- Allow **Ctrl+M** to be discrete from **Enter**.

## Commands
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.
- Add a way to reset or trim the history, when there's only one (or zero) clink running [#499](https://github.com/mridgers/clink/issues/499).
- Remember previous directory, and `-` swaps back to it [#372](https://github.com/mridgers/clink/issues/372).
  - Maybe set a `CLINK_PREV_DIR` envvar, too?
  - Remember a stack of previous directories?
- A way to disable/enable clink once injected.
- A way to disable/enable prompt filtering once injected.
- Allow to search the console output (not command history) with a RegExp [#166](https://github.com/mridgers/clink/issues/166).
  - Ideally enable lua to do searches and set scroll position, so it can be extensible -- e.g. bind a key to a lua script to search for next/prev line with red or yellow colored text, or to search for "error:", or etc.  Think of the possibilities!
- Enable lua to indicate the match type (word, file, dir, link)?

# EVENTUALLY

## Input
- Try to make unbound keys like **Shift-Left** tell conhost that they haven't been handled, so conhost can do its fancy CUA marking.
- Especially don't handle **ALt+F4**, unless maybe to bind it to a keyboard macro for the **Esc** input sequence plus "exit" plus **Enter**.
- Holding down a bound key like **Ctrl+Up** lets conhost periodically intercept some of the keypresses!
- The history does not record the 2nd+ lines of '^' escaped lines [#427](https://github.com/mridgers/clink/issues/427).

## Editing
- Custom color for readline input.  Might prefer to completely replace readline's line drawing, since it's trying to minimize updates over terminal emulators, and that makes it much harder to colorize the editing line (and arguments).
- [#532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines
  - It's pretty risky to just paste-and-go.
  - Maybe add an option to convert newlines into "&" instead?
  - Or maybe let readline do multiline editing and accept them all as a batch on **Enter**?

## Key Bindings
- **https://invisible-island.net/xterm/modified-keys.html**
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc.
- Translate terminal sequences into "C-A-S-name" in `show-rl-help`.
- Implement modes so it can be compatible with v0.4.9 key sequences?

## Commands
- Need a way to do `menu-complete` without wrapping around.
- Popup completion list.
- Popup history list.
- Expand environment variable.
- Marking mode?  It's a kludge, but it copies with HTML formatting (and even uses the color scheme).
- Doskey alias completion?  Seems like that should be done via a lua script?
- **F8** should behave like `history-search-backward` but wrap around.
- Complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.

## CUA EDITING
- Select all.
- **Shift+arrows** and etc do normal CUA style editing _[or maybe just let the new conhost handle that automagically?]_
- **Ctrl+C** do copy when CUA selection exists (might need to just intercept input handling, similar to how **Alt+H** was being intercepted), otherwise do Ctrl+C (Break).
- **Ctrl+X** cut selection.
- Custom color for CUA selected text.

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

# APPENDICES

## Mystery Issues
- [540](https://github.com/mridgers/clink/issues/540) v0.4.9 works but v1.0.0.a1 crashes in directory with too many files _[NOT REPRO: tried with ConEmu 200713 and chrisant996/clink head, switching to c:\Windows\System32]_

---
Chris Antos - sparrowhawk996@gmail.com
