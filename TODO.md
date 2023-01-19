_This todo list describes ChrisAnt996's current intended roadmap for Clink's future.  It is a living document and does not convey any guarantee about when or whether any item may be implemented._

<br/>

# IMPROVEMENTS

## High Priority
- Fix completion issue #407:
  - The `.` prefix support is broken; the list of selected matches doesn't include e.g. `.test`, but the match iterator loops over even the unselected matches.
    - The order of matches is wrong; `.test` should come before `test`.  This is because the pattern matching iteration is implemented wrongly.
    - The longest common prefix for `complete` is wrong; that command should not use the `.` prefix support.
  - Really matches need to be **selected** differently (not generated differently) based on whether `complete` and other prefix-based completion commands are used, versus `menu-complete` or `possible-completions` etc.

## Normal Priority
- Provide Lua APIs for `wildmatch()` and `fnmatch()`.
  - [ ] The flags may be a little tricky to handle reasonably.
  - [ ] Provide a recursive globbing function.  Maybe look for an implementation that optimizes away recursive paths that cannot match?

## Low Priority
- Consider not redrawing while resizing the terminal, if there is no RPROMPT?  Maybe just flag that a full redraw needs to happen, and defer it until the next time a redraw is normally requested?
- Allow removing event handlers, e.g. `clink.onbeginedit(func)` to add an event handler, and something like `clink.onbeginedit(func, false)` or `clink.removebeginedit(func)` to remove one?  Or maybe return a function that can be called to remove it, e.g. like below (but make sure repeated calls become no-ops).  The `clink-diagnostics` command would need to still show any removed event handlers until the next beginedit.  But it gets tricky if `func` is already registered -- should the new redundant registration's removal function be able to remove the pre-existing event handler?
    ```
    local remove = clink.onbeginedit(func) -- add func
    remove()                               -- remove func
    ```
- Allow Lua to set the comment row for the input line?
  - Need a simple and reliable trigger for clearing the comment row later; maybe `clink.onaftercommand()` is enough?
  - Don't add this ability unless there is a way to ensure comment rows don't get "leaked" and continue showing up past when they were relevant.
- Some way for `os.globfiles()` and `os.globdirs()` to override the `files.hidden` and `files.system` settings?
- Make a reusable wrapper mechanism to create coroutine-friendly threaded async operations in Lua?
- Issue #387 is a request to add an option to put the cursor at the end of the search text when using `history-substring-search-backward` (and `-forward`).  But that diverges even more from Readline, and I'm actively trying to instead converge as much as possible.  Maybe if Chet approves?

## Follow Up
- Push update to z.lua repo.

## Argmatcher syntax
- See the argmatcher_syntax branch.

<br/>
<br/>

# APPENDICES

## Known Issues
- `foo bar a/b/c` will try to expand `a/b/c` as an abbreviated path even if `foo bar` never generates filename matches.  This is a case that Clink can't really get perfectly right anymore, because of the automatic deduction of whether to use file matches.  Overall, this seems acceptable.
- Readline's `expand_tilde()` doesn't handle embedded `{space}{tilde}{pathsep}` correctly in strings; `rl.expandtilde()` does, and has an optional parameter to use Readline's original style of tilde expansion.
- Cursor style may behave unexpectedly in a new console window launched from a Windows Terminal console, or in a console window that gets attached to Windows Terminal.  This is because there's no reliable way for Clink to know whether it is running inside Windows Terminal.  Related to [Terminal #7434](https://github.com/microsoft/terminal/issues/7434).
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).
- [#369](https://github.com/chrisant996/clink/issues/369) and [mridgers#531](https://github.com/mridgers/clink/issues/531) anti-malware suites sometimes think Clink is malicious _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There's no way around that.  Signing the binaries might reduce that, but that's financially expensive and there's no way for an indepedent author to get an EV code signing certificate even if they were willing to pay the thousands of dollars per year.]_
- [Terminal #10191](https://github.com/microsoft/terminal/issues/10191#issuecomment-897345862) Microsoft Terminal does not allow a console application to know about or access the scrollback history, nor to scroll the screen.  It blocks Clink's scrolling commands, and also the `console.findline()` function and everything else that relies on access to the scrollback history.
- The auto-updater settings are stored in the profile.  That makes it more cumbersome to control the auto-updater settings if you use multiple Clink profiles.  However, it makes it possible to control the auto-updater settings separately in "portable installs" (e.g. on a USB memory stick).

## Mystery
- Once in a while raw mouse input sequences spuriously show up in the edit line; have only noticed it when the CMD window did not have focus at the time.  _[Not fixed by bb870fc494.]_ _[Have not seen for many weeks.]_
- Mouse input toggling is unreliable in Windows Terminal, and sometimes ends up disallowing mouse input.  _[Might be fixed by bb870fc494?]_
- `"qq": "QQ"` in `.inputrc`, and then type `qa` --> infinite loop.  _[Was occurring in a 1.3.9 development build; but no longer repros in a later 1.3.9 build, and also does not repro in the 1.3.8 release build.]_
- Windows 10.0.19042.630 seems to have problems when using WriteConsoleW with ANSI escape codes in a powerline prompt in a git repo.  But Windows 10.0.19041.630 doesn't.
- Windows Terminal crashes on exit after `clink inject`.  The current release version was crashing (1.6.10571.0).  Older versions don't crash, and a locally built version from the terminal repo's HEAD doesn't crash.  I think the crash is probably a bug in Windows Terminal, not related to Clink.  And after I built it locally, then it stopped crashing with 1.6.10571.0 as well.  Mysterious...

## Punt
- Some way for `history.save false` to not do any disk IO for history, but still enable `clink history` to show the session's history (probably using Shared Memory).  _[Unfavorable cost vs benefit; expensive and complicated, while offering very little benefit beyond what could be achieved by simply applying ACLs and/or encryption to the profile directory, which is something that is best done externally from Clink.]_
- Coroutines can call `clink.refilterprompt()` and it immediately refilters while in the coroutine.  Should it instead set a flag to refilter after the coroutines have yielded?  _[It should be fine because only `line_editor_impl` has an input idle callback that runs Lua coroutines.]_
- Show time stamps in history popup?  _[Gets complicated because of horizontal scrolling.  Too many edge cases; the benefit is not worth the cost.]_
- Some way to push keys?  (Push keys to Clink; not to other processes.)  _[Just use `WScript.Shell.SendKeys` when needed.]_
- Should coroutines really be able to make Readline redraw immediately?  Should instead set a flag that the main coroutine responds to when it gains control again?  _[For now it seems fine; coroutines run during idle when waiting for input, so it should be safe to let the display code run.]_
- Readline should pass the timeout into the `rl_input_available_hook` callback function.  _[Not needed; the timeout is only for systems that need to use `select()`, and we don't need to.]_
- Fix order that isearch executes the extra pending command in Callback Mode.  REPRO: `^R` x `Right` p ==> "p" is inserted, _THEN_ `Right` is executed.  _[Readline Callback Mode bug that only malfunctions when `isearch-terminators` omits ESC; not worth tracking down.]_
- Maybe redefine keyboard driver for Alt+(mod)+Fn key sequences to be like Ubuntu?  _[Not worth the disruptive impact unless it causes some problem.]_
- Readline doesn't handle certain display cases correctly.  Rather than try to fix the Readline display, I've built an alternative display implementation.  Here are some notes on the Readline issues:
  - A prompt exactly the width of the terminal seems to add a newline between the prompt and the input line, which might result from a missing `_rl_term_autowrap` test.
    - `_rl_vis_botlin` ends up with an incorrect value.
    - Could commit d4f48721ea22258b7239748d0d78d843ba2820f1 be related to that, or maybe it didn't fully fix the problem?
    - Run `clink drawtest --width 31 --emulation emulate` to observe.
    - Maybe Readline doesn't emit `SPC CR` if the prompt ends exactly at the screen width?
  - Readline is trying to use simple arithmetic to figure out how to convert byte position to absolute position, but the arithmetic is wrong and the data structures don't facilitate solving the issues.
    - E.g. a prompt whose last line wraps TWICE and has one UTF8 multibyte character in the FIRST wrapped segment; cursor position on the final line is offset wrongly.
      - It looks like this line `nleft = cpos_buffer_position - pos;` is trying to reset `nleft` to only include positions on the current screen row, which then throws off the `woff` arithmetic.  It could maybe use modulus on the overall position, but that wouldn't account for double-wide characters that don't fit at the end of a screen row and wrap "early".
      - This code might be relevant:
          ```c
          /* This assumes that all the invisible characters are split
             between the first and last lines of the prompt, if the
             prompt consumes more than two lines. It's usually right */
          /* XXX - not sure this is ever executed */
          _rl_last_c_pos -= (wrap_offset-prompt_invis_chars_first_line);
          ```
    - E.g. a prompt whose last line wraps AT the screen width and contains multibyte UTF8 characters; cursor position near the beginning of the input line gets positioned incorrectly.
      - `_rl_last_c_pos` is negative on entry to `rl_redisplay()`.
      - Because the "yet another special case" logic is triggered incorrectly, and adjusts cpos incorrectly, which carries forward to future calls.
      - After disabling that logic, then the cursor still goes wrong when crossing the `woff` boundary.
        - Need another iDNA for that...
- Readline 8.1 has slight bug in `update_line`; type `c` then `l`, and it now identifies **2** chars (`cl`) as needing to be displayed; seems like the diff routine has a bug with respect to the new faces capability; it used to only identify `l` as needing to be displayed.  _[Moot; Clink no longer uses Readline's display implementation.]_
- Optional feature to simplify auto-path-separator after completion, like in `zsh`:  highlight `\` in a color, and if <kbd>Space</kbd> is the next input then replace the `"\"` with `" "`.  _[Not worth it; there is very little value, and there are many side effects, e.g. wrt autosuggest.  I got excited at first, but then I realized what I really need is a better way to signal for `menu-complete` to accept a directory it's inserted and start a new completion.  And then I realized, since completions are normalized as of commit 9ec9eb1b69 in v1.1.24, typing <kbd>\</kbd><kbd>Tab</kbd> goes from `foo\` to `foo\\` to `foo\bar`, so the scenario I had in mind is already fully solved in a simple and reliable way.]_
- Postpone:  Ideally the updater could have a way to run an embedded script in the newly installed version, to do any needed finalization.  But there isn't really a way to reliably determine whether it needs to run, nor to handle errors that may occur.  And a more reliable mechanism is to do upgrade steps on the next inject.
- There's no straightforward way to let Lua scripts change the current directory and have CMD pick up the changed state.  CMD maintains internal private state about what directory to use when running commands and programs.  Running `cd` is the only way to alter CMD's internal private state.
- Explore adjusting default colors to have better contrast with white/light backgrounds?  _[No, it is tilting at windmills.  Never mind about Clink; nothing else will work reasonably in light themes, either, at least not the way they're currently defined in Windows Terminal.  If Windows Terminal fixes its themes, then it will become possible to have a single set of color definitions in Clink that work well in both light and dark themes.  Until then, it doesn't make sense to make complicated attempts to overcome the broader external problems.]_
- Recognizer and argmatcher lookup should support `@` syntax; collecting words should support `@   command` syntax.  _[Not worth the effort; using `@` at the command prompt has no effect anyway.]_
- Make `clink-show-help` call out prefix key sequences, since they can behave in a confusing manner?  _[Complex present in a non-confusing way, and very rare to actually occur.  Not worth the investment at this time.]_
- Maybe deal with timeouts in keyboard input?  Could differentiate <kbd>Esc</kbd> versus <kbd>Esc</kbd>,<kbd>Esc</kbd> but is very dangerous because it makes input processing unpredictable depending on the CPU availability.  _[Too dangerous.  And turned out to not be the issue.]_
- Ability to rearrange and edit popup list items?  _[Can't realistically rearrange or edit history, due to how the history file format works.]_
- Using a thread to run globbers could let suggestions uses matches even with UNC paths.  _[But **ONLY** globbers would be safe; if anything else inside match generators tries to access the UNC path then it could hang.  So it's not really safe enough.]_
- Make scrolling key bindings work at the pager prompt.  Note that it would need to revise how the scroll routines identify the bottom line (currently they use Readline's bottom line, but the pager displays output past that point).  _[Low value; also, Windows Terminal has scrolling hotkeys that supersede Clink, and it can scroll regardless whether prompting for input.  Further, Windows Terminal is deprecating the ability for an app to scroll the screen anyway.]_
- Is it a problem that `update_internal()` gets called once per char in a key sequence?  Maybe it should only happen after a key that finishes a key binding?  _[Doesn't cause any noticeable issues.]_
- Provide API to show an input box?  But make it fail if used from outside a "luafunc:" macro.  _[Questionable usage pattern; just make the "luafunc:" macro invoke a standalone program (or even standalone Lua script) that can accept input however it likes.]_
- Classify queued input lines?  _[Low value, high cost; the module layer knows about coloring, but queued lines are handled by the host layer without ever reaching the module layer.  Also, the queued input lines ("More?") do not adhere to the current parsing assumptions; it would become necessary to carry argmatcher state across lines.  Also, argmatchers do not currently understand `(` or `)`.]_
- Support this quirk, or not?  <kbd>Esc</kbd> in conhost clears the line but does not reset the history index, but in Clink it resets the history index.  Affects F1, F2, F3, F5, F8.  _[Defer until someone explains why it's important to them.]_
- Additional ANSI escape codes.
  - `ESC[?47h` and `ESC[?47l` (save and restore screen) -- not widely supported, so I can't use it, and it's not worth emulating.  Which makes me very sad; no save + show popup + restore. 😭
  - `ESC[?1049h` and `ESC[?1049l` (enable and disable alternative buffer) -- not worth using or emulating; there's no way to copying between screens, so it can't help for save/popup/restore.
- Marking mode in-app is not realistic:
  - Windows Terminal is essentially dropping support for console APIs that read/write the screen buffer, particularly the scrollback buffer.
  - Different terminal hosts have different capabilities and limitations, so building a marking mode that behaves reasonably across all/most terminal hosts isn't feasible.
  - One of the big opportunities for terminal hosts is to provide enhancements to marking and copy/paste.
  - So it seems best to leave marking and copy/paste as something for terminal hosts to provide.
- Lua API to copy text to clipboard (plain text or HTML) is not realistic, for the same technical reasons as for marking mode.
- Would be nice to complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.  However, it's difficult to make that work reasonably in conjunction with path normalization.
- [ConsoleZ](https://github.com/cbucher/console) sometimes draws the prompt in the wrong color:  scroll up, then type => the prompt is drawn in the input color instead of in the default color.  It doesn't happen in conhost or ConEmu or Windows Terminal.  Debugging indicates Clink is _not_ redrawing the prompt, so it's entirely an internal issue inside ConsoleZ.
- Max input line length:
  - CMD has a max input buffer size of 8192 WCHARs including the NUL terminator.
  - ReadConsole does not allow more input than can fit in the input buffer size.
  - Readline allows infinite input size, and has no way to limit it.
  - There is a truncation problem here that does not exist without Clink.
  - However, even CMD itself silently fails to run an inputted command over 8100 characters, despite allowing 8191 characters to be input.
  - So I'm comfortable punting this for now.
- A way to disable/enable clink once injected.  _[Why?]_
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[Unrelated to Clink; the exact same behavior occurs with plain cmd.exe]_

---
Chris Antos - sparrowhawk996@gmail.com
