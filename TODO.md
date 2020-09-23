ChrisAnt Plans

# PRIORITY

- `dispatch_input` dispatches one key, but it needs to dispatch one _keyboard sequence_.  It turns out this is tightly related to the "unbound keys accidentally emit part of the key sequence as text" issue.
  - **The design problem:**&nbsp; Input is consumed until it matches a binding.  But if it matches a prefix of one or more bindings without matching any full bound chord, then the matched prefix is discarded and the binding recognizer resets and continues from that point in the input.
  - **A solution option:**&nbsp; When the input matches a prefix of one or more bindings without matching any full bound chord, rewind and send the input through a separate table of "all possible keyboard key sequences":
    - If it matches anything in that table, consume the match and do nothing with it.
    - If it matches nothing in that table, then it's ok for the behavior to be undefined -- either do the current existing behavior, or treat each key as an unquoted literal.

## Next

## LUA
- Lua support changed significantly.  Explore how to support backward compatibility for existing scripts.
  - Prompt filtering looks probably straightforward.
  - argmatcher looks potentially more complicated, but maybe I just don't understand the data structures well enough yet.

## Problems
- `tab_completer` should either operate inside a Readline function, or should have settings to specify key bindings for normal vs menu completion.
- Still some kind of readline/ecma48_terminal_out problem when cycling through previous history (cycle very far back)?
- Need a `clink-` version of `menu-complete`, `menu-complete-backward`, `possible-completions`, etc.
  - Readline's `new-complete` resets once there's only one match, so that the next **Tab** does a new completion based on the new content.  It's cool for `complete`, but for `menu-complete` it's problematic because there's no visual indicator at all when it's a directory, and even when it adds a space (only one filename match) it's a major departure from the CMD tab completion.  Make sure the `clink-` variants solve that.
- Allow conhost to handle **Shift+Left** and etc for CUA selection.
- reverse-i-search and **Esc** needs to print a line feed.

## Key Bindings
- Unbound special keys (**Alt+Shift+UP**, etc) accidentally emit _part_ of the key name as text.  It seems like a non-match halts evaluation as soon as it exhausts potential chord prefixes, and the rest of the sequence ends up as literal input.  _Sounds like `skip-csi-sequence` isn't set?_
  - Try to make unbound keys like **Shift-Left** tell conhost that they haven't been handled, so conhost can do its fancy CUA marking.
  - Especially don't handle **ALt+F4**.
- Holding down a bound key like **Ctrl+Up** lets conhost periodically intercept some of the keypresses!

## Commands and Features

## LUA Scripts
- git completions.
- git powerline prompt.
- Examine other things from cmder's collection of lua scripts.

## Questions
- What is `set-mark`?
- How does `reverse-search-history` work?
- How does `kill-line` work?

# SOON

## Problems
- Fancy completion includes executables on PATH, but in c:\repos\clink\.build\vs2019\bin\debug it lists "clink" but not "clink.bat" as a completion, even though clink.bat is in the current directory.
- Pager has an unmitigated edge case:  it essentially assumes that the prompt is 1 line.  But the prompt can be multiple lines, and some of the fancy prompts are.  So if the final page is full, a couple lines can scroll out of view without having been read.
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width makes master go haywire.  Probably more ecma48 terminal issues.

## Key Bindings
- Hook up stuff via commands instead of via hard-coded custom bindings, so that everything can be remapped and reported by `show-rl-help`.
  - I clink has its own key binding manager because readline doesn't have a way to select a user-defined keymap.  Maybe that would be a cleaner solution?  There seem to be a lot of weird corner cases with clink's input manager and binding manager (e.g. sometimes input gets stuck mysteriously).
  - Instead of having special hard-coded keys, use rl commands for all "top level" bindings -- only use clink bind and bind_resolver for modal keymaps while doing nested input inside some other command.
  - For modal keymaps, go ahead and use the clink binder, but dynamically build/augment the modal keymap from rl bindings (so that binding **Ctrl+P** to `scroll-page-up` uses a modal keymap where **Ctrl+P** is the pageup key).
- Translate terminal sequences into "C-A-name" in `show-rl-help` (e.g. "C-A-Up", "Ctrl-Home", "End", etc).

## Commands
- **Alt+Home/End** scroll to top/bottom of buffer.
- Expand environment variable.
- Scrolling mode:
  - Have commands for scrolling up/down by a page or line (or top/bottom of buffer).
  - The commands should each activate scrolling mode, and those same keys (and only those keys) should scroll while scrolling mode is active.
  - Because I don't want **Shift+PgUp/PgDn** hard-coded for scrolling.
- Make `tab_completer` include doskey aliases, and use a configurable color for them.
- Make an option so `tab_completer` appends the preferred path separator to completed directories when they start in column 1, for convenience when entering just a directory for streamlined chdir.
- A setting to sort directories before files in the completion matches list.
- A setting to disable prompt filtering.  Primarily for debugging purposes, but maybe useful in other scenarios as well.

# EVENTUALLY

## Problems
- Win10 console mode flag to support ANSI sequences and colors; seems to maybe be working already?

## Editing
- _The new bindable **Esc** isn't yet compatible with vi mode!_
- Custom color for readline input.  Might prefer to completely replace readline's line drawing, since it's trying to minimize updates over terminal emulators, and that makes it much harder to colorize the editing line (and arguments).
- Colorize arguments recognized by lua argument parsers!  Also colorize doskey macros.

## Key Bindings
- **https://invisible-island.net/xterm/modified-keys.html**
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc.
- Translate terminal sequences into "C-A-S-name" in `show-rl-help`.
- Implement modes so it can be compatible with v0.4.9 key sequences?

## Commands
- **F8** should behave like `history-search-backward` but wrap around.
- Need a way to do `menu-complete` without wrapping around.
- Popup completion list.
- Select all.
- Marking mode.
- Complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.

## Premake
- Premake5 generates things incorrectly:
  - clink_process.vcxproj has Debug Just My Code enabled, and the debug build has inline expansion disabled -- both of those cause the injected code to crash because it references other functions!
  - Various other settings are strangely inconsistent between projects (PDB generation, for example).

## CUA EDITING
- **Shift+arrows** and etc do normal CUA style editing _[or maybe just let the new conhost handle that automagically?]_
- **Ctrl+C** do copy when CUA selection exists (might need to just intercept input handling, similar to how **Alt+H** was being intercepted), otherwise do Ctrl+C (Break).
- **Ctrl+X** cut selection.
- Custom color for CUA selected text.

# FUTURE

## Cleanup
- Why did it change to make a copy of the DLL instead of simply using the original copy?

## Fancy
- **Async command prompt updating as a way to solve the delay in git repos.**
- **Bind keys to lua scripts?**
- Lua scripts able to implement scrolling behavior (e.g. to scroll to next/prev compiler error, or colored text, etc).

## Configuration

# ISSUES OF INTEREST [clink/issues](https://github.com/mridgers/clink/issues)
- [540](https://github.com/mridgers/clink/issues/540) v0.4.9 works but v1.0.0.a1 crashes in directory with too many files
- [532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines _[copy from CASH]_
- [531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[or on execution, for me]_
- [486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [456](https://github.com/mridgers/clink/issues/456) clear screen not working when prompt is 2 lines long
- [453](https://github.com/mridgers/clink/issues/453) non-printable characters mess up rendering vs caret position
  - Fixed?
- [442](https://github.com/mridgers/clink/issues/442) paste is limited to 1024 characters
- [434](https://github.com/mridgers/clink/issues/434) history stops working when `--quiet` is used
- [x] [422](https://github.com/mridgers/clink/issues/422) filename modifier only works with forward slashes; needs to support backslashes
- _...need to examine the rest, from 421 downward..._

# UNIT TEST
- Over 39 thousand assertions?!

---
Chris Antos - sparrowhawk996@gmail.com
