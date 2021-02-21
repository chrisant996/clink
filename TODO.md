_This todo list describes ChrisAnt996's current intended roadmap for Clink's future.  It is a living document and does not convey any guarantee about when or whether any item may be implemented._

<br/>

# RELEASE

## Issues

## Cmder
- Port Cmder to v1.x -- will require help from Cmder and/or ConEmu teams.  There are a lot of hard-coded expectations about Clink (web site address, terminal input mode, DLL names, VirtualAlloc patterns, and many other things).

<br/>
<br/>

# IMPROVEMENTS

## High Priority
- **CUA Selection.**  A mode where rl_mark to rl_point is "selected", similar to the active mark mode.  If the cursor moves or the text changes then the selection automatically gets deactivated.  Modifying the line generally needs to delete the selected text before performing whatever editing operation was invoked.  The key design challenge here is to integrate into Readline with minimal changes that won't require ongoing maintenance.
- **Interactive completion.**  Similar to <kbd>Ctrl</kbd>+<kbd>Space</kbd> in Powershell and `menu-select` in zsh, etc.  The edge cases can get weird...

## Medium Priority
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc (see https://invisible-island.net/xterm/modified-keys.html).
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.  In the meantime, `operate-and-get-next` achieves the same result albeit with a slightly different (and more efficient) workflow.
- Symlink support (displaying matches, and whether to append a path separator).
- Provide a way for a custom classifier to apply a classification anywhere (not just to a pre-parsed word), and to apply any arbitrary CSI SGR code to a word or to anywhere.

## Low Priority
- Make scrolling key bindings work at the pager prompt.  Note that it would need to revise how the scroll routines identify the bottom line (currently they use Readline's bottom line, but the pager displays output past that point).
- Add a hook function for inserting matches.
  - The insertion hook can avoid appending a space when inserting a flag/arg that ends in `:` or `=`.
  - And address the sorting problem, and then the match_type stuff can be removed from Readline itself (though Chet may want its performance benefits).
  - And THEN individual matches can have arbitrary values associated -- color, append char, or any per-match data that's desired.
  - But the hard part is handling duplicates (especially with different match types).  Could maybe pass the index in the matches array, but that requires tighter interdependence between Readline and its host.

<br/>
<br/>

# INVESTIGATE

**Documentation**
- Use `npm` to run `highlight.js` at compile time?

**Popup Lists**
- Ability to delete, rearrange, and edit popup list items?
- Show the current incremental search string somewhere?
- Completions:
  - What to do about completion colors?
  - Make it owner draw and add text like "dir", "alias", etc?
  - Add stat chars when so configured?

**Prompt Filtering**
- Async command prompt updating as a way to solve the delay in git repos.

**Marking**
- Marking mode in-app similar to my other shell project?  It's a kludge, but it copies with HTML formatting (and even uses the color scheme).

**Lua**
- Provide API to generate HTML string from console text.
- Provide API to copy text to clipboard (e.g. an HTML string generated from console text).
- Is there some way to show selection markup?  Maybe have a way to have floating windows mark corners of a selection region, or overlay or or more windows to draw an outline around the selected region?
- Provide API to show a popup list?  But make it fail if used from outside a Readline command.
- Provide API to show an input box?  But make it fail if used from outside a Readline command.

**Readline**
- Readline 8.1 has slight bug in `update_line`; type `c` then `l`, and it now identifies **2** chars (`cl`) as needing to be displayed; seems like the diff routine has a bug with respect to the new faces capability; it used to only identify `l` as needing to be displayed.

**Miscellaneous**
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width works in master, except when the cursor position itself is affected.
- Is it a problem that `update_internal()` gets called once per char in a key sequence?  Maybe it should only happen after a key that finishes a key binding?
- Should only fold path separators in pathish matches.
- How to reasonably support normal completion coloring with `ondisplaymatches` match display filtering?
- Include `wildmatch()` and an `fnmatch()` wrapper for it.  But should first update it to support UTF8.
- `LOG()` certain important failure information inside Detours.

<br/>
<br/>

# MAINTENANCE

## Remove match type changes from Readline?
- Displaying matches was slow because Readline writes everything one byte at a time, which incurs significant processing overhead across several layers.
- Adding a new `rl_completion_display_matches_func` and `display_matches()` resolved the performance satisfactorily.
- It's now almost possible to revert the changes to feed Readline match type information.  It's only used when displaying matches, and when inserting a match to decide whether to append a path separator.  Could just add a callback for inserting.
- But there's a hurdle:
  - Readline sorts the matches, making it difficult to translate the sorted index to the unsorted list held by `matches_impl`.  Could use a binary search, but using binary search inside a sort comparator makes the algorithmic complexity O(N * logN * logN) which is even worse than O(N * N).

<br/>
<br/>

# BACKLOG

- [#542](https://github.com/mridgers/clink/issues/542) VS Code not capturing std output
- A way to disable/enable clink once injected.
- [#532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines
  - It's pretty risky to just paste-and-go.
  - Maybe add an option to convert newlines into "&" instead?
  - Or maybe let readline do multiline editing and accept them all as a batch on **Enter**?
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- [#20](https://github.com/chrisant996/clink/issues/20) Cmd gets unresponsive after "set /p" command.  _[Seems to mostly work, though `set /p FOO=""` doesn't prompt for input.]_

<br/>
<br/>

# APPENDICES

## Manual Test Verifications
- <kbd>Alt</kbd>+<kbd>Up</kbd> to scroll up a line.
- `git add ` in Cmder.
- `git checkout `<kbd>Alt</kbd>+<kbd>=</kbd> in Cmder.

## Known Issues
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).
- [#531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There might be a way to obfuscate the fact that clink uses those, but ultimately this is kind of an inherent problem.  Getting the binaries digitally signed might be the most effective solution, but that's financially expensive.]_
- [FIXED] Readline's incremental display updates plus its reliance on ANSI escape codes for cursor positioning seem to make it not able to properly support editing within a line containing surrogate pairs.  I would say that it's a Readline issue, except that git-bash seems to be a bit better at it than Clink is, so maybe Clink isn't hosting Readline correctly.  FIXED: Readline relies on wchar_t being 32 bits, so some shimming was needed to accomplish that when compiling with Visual Studio.

## Mystery
- [#480](https://github.com/mridgers/clink/issues/480) Things don't work right when clink is in a path with spaces _[I'm not able to reproduce the problem, so dropping it off the radar for now.]_
- Windows 10.0.19042.630 seems to have problems when using WriteConsoleW with ANSI escape codes in a powerline prompt in a git repo.  But Windows 10.0.19041.630 doesn't.

## Punt
- Would be nice to complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.  However, it's difficult to make that work reasonably in conjunction with path normalization.

---
Chris Antos - sparrowhawk996@gmail.com
