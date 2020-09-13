# ChrisAnt Plans

## PROBLEMS
- Key bindings aren't working properly
  - **Alt+Up/Down** in scroller.cpp don't get interpreted correctly
  - Many keys don't work correctly
  - `show-rl-help` doesn't seem to list bindings from .inputrc, nor internal custom bindings???
  - Hook up stuff via commands instead of via hard-coded custom bindings, so that everything can be remapped and reported by `show-rl-help`

## Basic
- **Alt+Up/Down** scroll one line
- **Alt+PgUp/PgDn** scroll one page
- **Alt+Home/End** scroll to top/bottom of buffer
- A directory by itself as the input should simply change to the directory (this is the main behavior in CASH that wasn't self-contained within the input editor code)
- Support ANSI sequences, etc (console mode flag)

## Commands
- Add line into history but clear editor without executing the line
- Delete current line from history (`unix-line-discard`)
- Cycle through completions (`menu-complete` and `menu-complete-backward`)
- Delete word left (`unix-word-rubout`)
- Delete word right (`kill-word` is almost the same, but deletes both the word to the right and also any whitespace before it)
- Previous history filtered (`history-search-backward`)
  - **F8** needs a variation that wraps around
- Next history filtered (`history-search-forward`)
- Expand alias into the editing buffer
- Popup completion list
- Select all
- Marking mode
- Accept input raw character: e.g. `some-new-command` followed by **Ctrl+G** to input `^G` (BEL) character (issue #541)
- Report the name of pressed key: e.g. `some-new-command` followed by **Key** to report `C-A-S-key` and/or the xterm sequence format readline uses

## CUA Editing
- **Shift+arrows** and etc do normal CUA style editing _[or maybe just let the new conhost handle that automagically?]_
- **Ctrl+C** do copy when CUA selection exists (might need to just intercept input handling, similar to how **Alt+H** was being intercepted), otherwise do Ctrl+C (Break)
- **Ctrl+X** cut selection

## Colors
- Custom color for readline input
- Custom color for CUA selected text

## Cleanup
- Show custom key bindings in `show-rl-help`
- Show friendly key names instead of xterm sequences in `show-rl-help`
- Fix VK mappings for **Arrow Keys**, **Backspace**, etc
- Ctrl+- isn't recognized; it comes through as Ctrl+_

## Fancy
- Bind keys to lua scripts
- Some persistent key binding mechanism (`clink_inputrc` allows this, but I would like the key bindings to use the same format the **Alt+H** reports)
- Lua scripts able to implement scrolling behavior (e.g. to scroll to next/prev compiler error, or colored text, etc)
- Async command prompt updating as a way to solve the delay in git repos
- Let lua scripts specify color for argument parsers

## ISSUES OF INTEREST [clink/issues](https://github.com/mridgers/clink/issues)
- [541](https://github.com/mridgers/clink/issues/541) input escaped characters
- [540](https://github.com/mridgers/clink/issues/540) v0.4.9 works by v1.0.0.a1 crashes in directory with too many files
- [532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines _[copy from CASH]_
- [531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[or on execution, for me]_
- [516](https://github.com/mridgers/clink/issues/516) $T not handled correctly in aliases
- [503](https://github.com/mridgers/clink/issues/503) some way to scroll up/down by one line
- [501](https://github.com/mridgers/clink/issues/501) **Backspace** vs **Ctrl+Backspace** vs **Del** don't work properly
- [486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [456](https://github.com/mridgers/clink/issues/456) clear screen not working when prompt is 2 lines long
- [453](https://github.com/mridgers/clink/issues/453) non-printable characters mess up rendering vs caret position
- [451](https://github.com/mridgers/clink/issues/451) doskey macros broken on Win10
  - Fixed by [PR 464](https://github.com/mridgers/clink/pull/464)
- [442](https://github.com/mridgers/clink/issues/442) paste is limited to 1024 characters
- [434](https://github.com/mridgers/clink/issues/434) history stops working when `--quiet` is used
- [422](https://github.com/mridgers/clink/issues/422) filename modifier only works with forward slashes; needs to support backslashes
- _...need to examine the rest, still..._

## UNIT TEST
- 1 failure
- tens of thousands of assertions?!

## QUESTIONS
- What is `set-mark`?
- How does `reverse-search-history` work?
- How does `kill-line` work?

---
Chris Antos - sparrowhawk996@gmail.com
