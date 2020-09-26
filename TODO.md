ChrisAnt Plans

# Phase 1

## Premake
- Premake5 generates things incorrectly:
  - use ProgramDatabase instead of EditAndContinue
  - disable OmitFramePointers
  - enable GenerateDebugInformation
  - clink_process
    - set InlineFunctionExpansion to be AnySuitable
    - disable SupportJustMyCode

## LUA
- Lua support changed significantly.  Explore how to support backward compatibility for existing scripts.
  - argmatcher looks potentially more complicated, but maybe I just don't understand the data structures well enough yet.

## Features
- **Ctrl+Backspace** needs to stop at path separators -- probably just means use a better command than `unix-word-rubout`.
- Setting to control persistent history.
- Support coloring directories differently in the completion list.
- Remember previous directory, and `-` swaps back to it.
  - Maybe set a `CLINK_PREV_DIR` envvar, too?
  - Remember a stack of previous directories?
- A way to disable/enable clink once injected.
- A way to disable/enable prompt filtering once injected.

### Tab Complete
- Need a `clink-` version of `menu-complete`, `menu-complete-backward`, `possible-completions`, etc (move `tab_completer` to be implemented as Readline commands).
  - Needs to support wildcards.
  - Readline's `next-complete` resets once there's only one match, so that the next **Tab** does a new completion based on the new content.  It's cool for `complete`, but for `menu-complete` it's problematic because there's no visual indicator at all when it's a directory, and even when it adds a space (only one filename match) it's a major departure from the CMD tab completion.  Make sure the `clink-` variants solve that.

### Scrolling mode
- Have commands for scrolling up/down by a page or line (or top/bottom of buffer).
- The commands should each activate scrolling mode, and those same keys (and only those keys) should scroll while scrolling mode is active.
  - Might need a hook in readline to do things before a command is processed, e.g. to exit scrolling mode when any non-scrolling command is invoked.
- Because I don't want **Shift+PgUp/PgDn** hard-coded for scrolling.

### Other
- Allow conhost to handle **Shift+Left** and etc for CUA selection?
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width makes master go haywire.  Probably more ecma48 terminal issues.

## Questions
- What is `set-mark`?
- How does `reverse-search-history` work?
- How does `kill-line` work?

# Issues Backlog [clink/issues](https://github.com/mridgers/clink/issues)
- [544](https://github.com/mridgers/clink/issues/544) Clink v1.0.0.a1 doesn't support cyrillic characters keyboard input
- [542](https://github.com/mridgers/clink/issues/542) VS Code not capturing std output
- [x] [540](https://github.com/mridgers/clink/issues/540) v0.4.9 works but v1.0.0.a1 crashes in directory with too many files
  - I can't reproduce this using ConEmu 200713.  Checking this off for now, pending further information.
- [532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines
  - It's pretty risky to just paste-and-go.
  - Maybe add an option to convert newlines into "&" instead?
  - Or maybe let readline do multiline editing and accept them all as a batch on **Enter**?
- [531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[or on execution, for me]_
- [519](https://github.com/mridgers/clink/issues/519) Clink v1.0.0.a1 - `-s|--scripts [path]` command line arg removed?
- [502](https://github.com/mridgers/clink/issues/502) Error in folders containing [ ] characters
- [486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [480](https://github.com/mridgers/clink/issues/480) Things don't work right when clink is in a path with spaces
- [x] [453](https://github.com/mridgers/clink/issues/453) non-printable characters mess up rendering vs caret position
  - _Double check that it's fixed by the Readline update, as advertised._
- [x] [422](https://github.com/mridgers/clink/issues/422) filename modifier only works with forward slashes; needs to support backslashes
  - Working in chrisant996/clink.
- [415](https://github.com/mridgers/clink/issues/415) Different encodings in different lua functions
- [398](https://github.com/mridgers/clink/issues/398) Cmd gets unresponsive after "set /p" command.
- [396](https://github.com/mridgers/clink/issues/396) Pasting unicode emoji in a clink-enabled console
- [365](https://github.com/mridgers/clink/issues/365) history-search behavior _(put cursor at end of command line when searching forward/backward in the history)_
- [30](https://github.com/mridgers/clink/issues/30) wildcards not expanded.

# Phase 2

## Problems
- _The new bindable **Esc** isn't yet compatible with vi mode!_
- Win10 console mode flag to support ANSI sequences and colors; seems to maybe be working already?
- Investigate [XTerm 256 support](https://conemu.github.io/en/AnsiEscapeCodes.html) [#487](https://github.com/mridgers/clink/issues/487).

## Key Bindings
- Hook up stuff via commands instead of via hard-coded custom bindings, so that everything can be remapped and reported by `show-rl-help`.
  - I think clink has its own key binding manager because readline doesn't have a way to select a user-defined keymap, and because readline commands are global and aren't very OOP-friendly.
  - Instead of having special hard-coded keys, use rl commands for all "top level" bindings -- only use clink bind and bind_resolver for modal keymaps while doing nested input inside some other command?
  - For modal keymaps, go ahead and use the clink binder, but dynamically build/augment the modal keymap from rl bindings (so that binding **Ctrl+P** to `scroll-page-up` uses a modal keymap where **Ctrl+P** is the pageup key).
- I want `show-rl-help` to be able to list enhanced keys like Up, Home, Ctrl-Shift-Space, etc.
  - Translate terminal sequences into "C-A-name" in `show-rl-help` (e.g. "C-A-Up", "Ctrl-Home", "End", etc)?  But that gets weird because those aren't parseable key names.
  - Maybe have two variants of `show-rl-help` -- one that shows human readable key names, and one that shows the actual binding strings?
  - Invent an alternative syntax?

## Commands
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.
- Add a way to reset or trim the history, when there's only one (or zero) clink running [#499](https://github.com/mridgers/clink/issues/499).

## Unit Test
- Over 39 thousand assertions?!

# EVENTUALLY

## Input
- Try to make unbound keys like **Shift-Left** tell conhost that they haven't been handled, so conhost can do its fancy CUA marking.
- Especially don't handle **ALt+F4**, unless maybe to bind it to a keyboard macro for the **Esc** input sequence plus "exit" plus **Enter**.
- Holding down a bound key like **Ctrl+Up** lets conhost periodically intercept some of the keypresses!

## Editing
- Custom color for readline input.  Might prefer to completely replace readline's line drawing, since it's trying to minimize updates over terminal emulators, and that makes it much harder to colorize the editing line (and arguments).
- [532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines _[copy from CASH]_

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
- Make `tab_completer` include doskey aliases?
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

## Configuration

# APPENDICES

## Mystery Issues
- [540](https://github.com/mridgers/clink/issues/540) v0.4.9 works but v1.0.0.a1 crashes in directory with too many files _[NOT REPRO: tried with ConEmu and chrisant996/clink head, switching to c:\Windows\System32]_

---
Chris Antos - sparrowhawk996@gmail.com
