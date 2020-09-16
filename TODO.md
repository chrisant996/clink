ChrisAnt Plans

# PRIORITY
- Custom color for readline input.
- Make filename modifier also work for backslashes.

## Problems
- Why did pagination disappear from **Alt+H**?  _UPDATE:  Because it's another hard-coded key binding now, and pagination isn't implemented yet (could be implemented similar to `tab_completer`, but it really needs to be inside a readline command and push a nested input loop)._
- Why can't tab_completer's pretty printing and pagination be inside a `rl_completion_matches_display_hook`?  Why does it need to hard-code a binding for **Tab**?  _UPDATE:  Because of the `on_input` state machine?  `input_dispatcher::dispatch` now allows nested input processing._

## Key Bindings
- Unbound special keys (**Alt+Shift+UP**, etc) accidentally emit _part_ of the key name as text.  It seems like a non-match halts evaluation as soon as it exhausts potential chord prefixes, and the rest of the sequence ends up as literal input.  _Sounds like `skip-csi-sequence` isn't set?_
- "\C-@" is supposed to work for **Ctrl+Space** but doesn't work for **Ctrl+Shift+2** nor for **Ctrl+Space**.
  - _UPDATE:  Although `bind_resolver` tracks m_key_count and generates `chord` correctly, both `bind` and `editor_module::input` assume a nul terminated string._

## Commands and Features
- Add line into history but clear editor without executing the line.
- Delete current line from history.
- Expand alias into the editing buffer.
- A directory by itself as the input should simply change to the directory (this is the main behavior in CASH that wasn't self-contained within the input editor code).
- Accept input raw character: e.g. `some-new-command` followed by **Ctrl+G** to input `^G` (BEL) character (issue #541).
- Report the name of pressed key: e.g. `some-new-command` followed by **Key** to report `C-A-S-key` and/or the xterm sequence format readline uses.

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

## Key Bindings
- Hook up stuff via commands instead of via hard-coded custom bindings, so that everything can be remapped and reported by `show-rl-help`.
  - I clink has its own key binding manager because readline doesn't have a way to select a user-defined keymap.  Maybe that would be a cleaner solution?  There seem to be a lot of weird corner cases with clink's input manager and binding manager (e.g. sometimes input gets stuck mysteriously).
  - Instead of having special hard-coded keys, use rl commands for all "top level" bindings -- only use clink bind and bind_resolver for modal keymaps while doing nested input inside some other command.
  - For modal keymaps, go ahead and use the clink binder, but dynamically build/augment the modal keymap from rl bindings (so that binding **Ctrl+P** to `scroll-page-up` uses a modal keymap where **Ctrl+P** is the pageup key).
- Translate terminal sequences into "C-A-name" in `show-rl-help` (e.g. "C-A-Up", "Ctrl-Home", "End", etc).

## Commands
- **Alt+Home/End** scroll to top/bottom of buffer.
- Expand environment variable.

# EVENTUALLY

## Problems
- Win10 console mode flag to support ANSI sequences and colors; seems to maybe be working already?

## Commands
- **F8** should behave like `history-search-backward` but wrap around.
- Need a way to do `menu-complete` without wrapping around.
- Popup completion list.
- Select all.
- Marking mode.
- Complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.

## Key Bindings
- **https://invisible-island.net/xterm/modified-keys.html**
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc.
- Translate terminal sequences into "C-A-S-name" in `show-rl-help`.
- Implement modes so it can be compatible with v0.4.9 key sequences?

## Editing
- _The new bindable **Esc** isn't yet compatible with vi mode!_
- `kill-whole-line` leaves old draw state:  type "echo ", **Alt+Shift+8**, **Esc** => clears the line, but "echo" remains!

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
- Why did it change from clink_dll_x64.dll to clink_x64.dll?  The old way was much simpler for debugging with PDB.
- Why did it change to make a copy of the DLL instead of simply using the original copy?

## Fancy
- Bind keys to lua scripts.
- Some persistent key binding mechanism (`clink_inputrc` allows this, but I would like the key bindings to use the same format the **Alt+H** reports).
- Lua scripts able to implement scrolling behavior (e.g. to scroll to next/prev compiler error, or colored text, etc).
- Async command prompt updating as a way to solve the delay in git repos.
- Let lua scripts specify color for argument parsers.
- Scrolling mode:
  - Have commands for scrolling up/down by a page or line (or top/bottom of buffer).
  - The commands should each activate scrolling mode, and those same keys (and only those keys) should scroll while scrolling mode is active.
  - Because I don't want **Shift+PgUp/PgDn** hard-coded for scrolling.

## Configuration

# ISSUES OF INTEREST [clink/issues](https://github.com/mridgers/clink/issues)
- [541](https://github.com/mridgers/clink/issues/541) input escaped characters
- [540](https://github.com/mridgers/clink/issues/540) v0.4.9 works but v1.0.0.a1 crashes in directory with too many files
- [532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines _[copy from CASH]_
- [531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[or on execution, for me]_
- [516](https://github.com/mridgers/clink/issues/516) $T not handled correctly in aliases
- [x] [503](https://github.com/mridgers/clink/issues/503) some way to scroll up/down by one line
- [x] [501](https://github.com/mridgers/clink/issues/501) **Backspace** vs **Ctrl+Backspace** vs **Del** don't work properly
- [486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [456](https://github.com/mridgers/clink/issues/456) clear screen not working when prompt is 2 lines long
- [453](https://github.com/mridgers/clink/issues/453) non-printable characters mess up rendering vs caret position
- [451](https://github.com/mridgers/clink/issues/451) doskey macros broken on Win10
  - Fixed by [PR 464](https://github.com/mridgers/clink/pull/464)
- [442](https://github.com/mridgers/clink/issues/442) paste is limited to 1024 characters
- [434](https://github.com/mridgers/clink/issues/434) history stops working when `--quiet` is used
- [422](https://github.com/mridgers/clink/issues/422) filename modifier only works with forward slashes; needs to support backslashes
- _...need to examine the rest, still..._

# UNIT TEST
- 1 failure.
- tens of thousands of assertions?!

---
Chris Antos - sparrowhawk996@gmail.com
