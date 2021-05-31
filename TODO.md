_This todo list describes ChrisAnt996's current intended roadmap for Clink's future.  It is a living document and does not convey any guarantee about when or whether any item may be implemented._

<br/>

# RELEASE

## Issues

## Investigate

#### _Prompt Filtering_
- Async command prompt updating as a way to solve the delay in git repos.
  - [x] Use `clink.addcoroutine()` to register a coroutine to resume while waiting for input.
  - [x] Use `clink.refilterprompt()` to rerun prompt filtering and redisplay.
  - [x] win_terminal_in idle callback.
  - [x] host_lua supply idle callback.
  - [x] Prompt filter can create coroutine for deferred work:
    - [x] No-op if already created.
    - [x] Supply cached result if finished.
    - [x] Coroutine provides its result when finished:  cache result and call `clink.refilterprompt()`.
  - [x] Determine wait strategy.
    - [x] `clink.promptfilter()` accepts optional frequency in seconds with millisecond precision.
    - [x] ~~Allow a configurable delay before showing the prompt if any `io.popenyield()` have been used?  So that for example in small git repos the prompt color doesn't flicker briefly.~~
  - [x] There needs to be a way to perform non-blocking IO.
    - [x] Maybe some kind of `io.popenyield()` API that signals an event when ready.
    - [x] Associate wake event with `popenbuffering*` so yield can be resumed immediately on completion.
    - [x] Associate yieldguard with coroutine.
    - [x] If any coroutine has no yieldguard then resume immediately (subject to throttling).
  - [ ] Enforce good behavior from coroutines.
    - [x] Event to signal ready for resume (e.g. from `os.popenyield()`).
    - [ ] Throttle individual greedy coroutines.
  - [ ] Need a way to show visible clues as to what's happening with waits and coroutines.

#### _General_
- Add syntax for argmatchers to defer adding args/flags, to facilitate adding args/flags by parsing help text from a program.  This is more complex than I first thought:
  - It gets overly complicated for a script to handle arg2 or deeper (needs list of preceding args, preceding flags for args, etc -- not to mention linked parsers).
  - To support input line coloring it needs to run code simply due to input from the user, regardless whether any completion is invoked.
  - It should not block while waiting for an external app to run.  This suggests maybe using Lua coroutines, but then:
    - How to avoid blocking while waiting for piped output to complete?
    - How to make it difficult for a script to deviate from the efficient non-blocking pattern?
  - How to make it clear what a script needs to supply?  E.g. for which arg(s) and flag(s) and which command, etc?

## Mystery

<br/>
<br/>

# IMPROVEMENTS

## High Priority

## Medium Priority
- Symlink support (displaying matches, and whether to append a path separator).

## Low Priority
- Maybe `"-foo="..parser("a", "b", "c")` could actually be detected somehow?  Maybe it could adjust the parsed word list in response to the existence of the arglink?
- Make scrolling key bindings work at the pager prompt.  Note that it would need to revise how the scroll routines identify the bottom line (currently they use Readline's bottom line, but the pager displays output past that point).
- Add a hook function for inserting matches.
  - The insertion hook can avoid appending a space when inserting a flag/arg that ends in `:` or `=`.
  - And address the sorting problem, and then the match_type stuff can be removed from Readline itself (though Chet may want its performance benefits).
  - And THEN individual matches can have arbitrary values associated -- color, append char, or any per-match data that's desired.
  - But the hard part is handling duplicates (especially with different match types).  Could maybe pass the index in the matches array, but that requires tighter interdependence between Readline and its host.
- **Interactive completion.**  Similar to <kbd>Ctrl</kbd>+<kbd>Space</kbd> in Powershell and `menu-select` in zsh, etc.  The edge cases can get weird...
  - Oh but the new `clink.onfiltermatches()` might be even better since it enables integration with custom completion filters (e.g. `fzf`).

## Tests

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

**Installer**
- Why does it install to a versioned path?  The .nsi file says `; Install to a versioned folder to reduce interference between versions.` so use caution when making any change there.

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

- A way to disable/enable clink once injected.
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_

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
- Windows Terminal crashes on exit after `clink inject`.  The current release version was crashing (1.6.10571.0).  Older versions don't crash, and a locally built version from the terminal repo's HEAD doesn't crash.  I think the crash is probably a bug in Windows Terminal, not related to Clink.  And after I built it locally, then it stopped crashing with 1.6.10571.0 as well.  Mysterious...
- Corrupted clink_history -- not sure how, when, or why -- but after having made changes to history, debugging through issues, and aborting some debugging sessions my clink_history file had a big chunk of contiguous NUL bytes. _[UPDATE: the good news is it isn't a Clink issue; the bad news is the SSD drives in my new Alienware m15 R4 keep periodically hitting a BSOD for KERNEL DATA INPAGE ERROR, which zeroes out recently written sectors.]_

## Punt
- Would be nice to complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.  However, it's difficult to make that work reasonably in conjunction with path normalization.
- [ConsoleZ](https://github.com/cbucher/console) sometimes draws the prompt in the wrong color:  scroll up, then type => the prompt is drawn in the input color instead of in the default color.  It doesn't happen in conhost or ConEmu or Windows Terminal.  Debugging indicates Clink is _not_ redrawing the prompt, so it's entirely an internal issue inside ConsoleZ.
- Max input line length:
  - CMD has a max input buffer size of 8192 WCHARs including the NUL terminator.
  - ReadConsole does not allow more input than can fit in the input buffer size.
  - Readline allows infinite input size, and has no way to limit it.
  - There is a truncation problem here that does not exist without Clink.
  - However, even CMD itself silently fails to run an inputted command over 8100 characters, despite allowing 8191 characters to be input.
  - So I'm comfortable punting this for now.

---
Chris Antos - sparrowhawk996@gmail.com
