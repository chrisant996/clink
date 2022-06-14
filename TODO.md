_This todo list describes ChrisAnt996's current intended roadmap for Clink's future.  It is a living document and does not convey any guarantee about when or whether any item may be implemented._

<br/>

# IMPROVEMENTS

## High Priority
- Sometimes completion isn't working, but I haven't found consistent repro steps yet.
  - Could potentially be the cause of below as well, since delayinit happens only once?
- Sometimes delayinit argmatchers (e.g. `premake`) aren't initializing at all.
  - Seems to be a race condition.
  - Can repro with simply launch, cd, `pre` and <kbd>Up</kbd> several times rapidly -- but it's very hard to make it happen on demand.
  - Diag observations; true regardless whether init is successful (non-empty).
    - Multiple dead 'generate matches' coroutines.
    - Dead coroutine from ?1687 (the coroutine created in `_do_onuse_callback`).
    - Generators stopped by ?:1863 (`argmatcher_generator:generate`).

## Normal Priority
- Some way to have e.g. multiple separate `ut` argmatchers that are associated with different `ut` program paths.
- Some way to push keys?  (Push keys to Clink; not to other processes.)

## Low Priority
- Once in a while raw mouse input sequences spuriously show up in the edit line; have only noticed it when the CMD window did not have focus at the time.  _[Not fixed by bb870fc494.]_
- `console.readinput()` changes the console state, and can interfere with proper line wrapping behavior.
- Collecting words currently happens in update_internal, but probably it also belongs in alternative_matches and/or update_matches:
  - If a `luafunc:` macro first does anything that alters the line buffer, and then invokes a completion command, then the collected words will be inaccurate.
- Should coroutines really be able to make Readline redraw immediately?  Should instead set a flag that the main coroutine responds to when it gains control again?

## Follow Up
- Update other repos:
  - [ ] Add command and flag descriptions in clink-completions?
  - [ ] Push update to clink-completions repo.
  - [ ] Push update to z.lua repo.

## Argmatcher syntax
- See the argmatcher_syntax branch.

<br/>
<br/>

# APPENDICES

## Manual Test Verifications
- <kbd>Alt</kbd>+<kbd>Up</kbd> to scroll up a line.
- `git add ` in Cmder.
- `git checkout `<kbd>Alt</kbd>+<kbd>=</kbd> in Cmder.

## Known Issues
- Readline 8.1 has slight bug in `update_line`; type `c` then `l`, and it now identifies **2** chars (`cl`) as needing to be displayed; seems like the diff routine has a bug with respect to the new faces capability; it used to only identify `l` as needing to be displayed.
- Cursor style may behave unexpectedly in a new console window launched from a Windows Terminal console, or in a console window that gets attached to Windows Terminal.  This is because there's no reliable way for Clink to know whether it is running inside Windows Terminal.
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).
- [#531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There might be a way to obfuscate the fact that clink uses those, but ultimately this is kind of an inherent problem.  Getting the binaries digitally signed might be the most effective solution, but that's financially expensive.]_
- [Terminal #10191](https://github.com/microsoft/terminal/issues/10191#issuecomment-897345862) Microsoft Terminal does not allow a console application to know about or access the scrollback history, nor to scroll the screen.  It blocks Clink's scrolling commands, and also the `console.findline()` function and everything else that relies on access to the scrollback history.
- The auto-updater settings are stored in the profile.  That makes it more cumbersome to control the auto-updater settings if you use multiple Clink profiles.  However, it makes it possible to control the auto-updater settings separately in "portable installs" (e.g. on a USB memory stick).

## Mystery
- Mouse input toggling is unreliable in Windows Terminal, and sometimes ends up disallowing mouse input.  _[Might be fixed by bb870fc494?]_
- `"qq": "QQ"` in `.inputrc`, and then type `qa` --> infinite loop.  _[Was occurring in a 1.3.9 development build; but no longer repros in a later 1.3.9 build, and also does not repro in the 1.3.8 release build.]_
- Windows 10.0.19042.630 seems to have problems when using WriteConsoleW with ANSI escape codes in a powerline prompt in a git repo.  But Windows 10.0.19041.630 doesn't.
- Windows Terminal crashes on exit after `clink inject`.  The current release version was crashing (1.6.10571.0).  Older versions don't crash, and a locally built version from the terminal repo's HEAD doesn't crash.  I think the crash is probably a bug in Windows Terminal, not related to Clink.  And after I built it locally, then it stopped crashing with 1.6.10571.0 as well.  Mysterious...

## Punt
- Optional feature to simplify auto-path-separator after completion, like in `zsh`:  highlight `\` in a color, and if <kbd>Space</kbd> is the next input then replace the `"\"` with `" "`.  _[Not worth it; there is very little value, and there are many side effects, e.g. wrt autosuggest.  I got excited at first, but then I realized what I really need is a better way to signal for `menu-complete` to accept a directory it's inserted and start a new completion.  And then I realized, since completions are normalized as of commit 9ec9eb1b69 in v1.1.24, typing <kbd>\</kbd><kbd>Tab</kbd> goes from `foo\` to `foo\\` to `foo\bar`, so the scenario I had in mind is already fully solved in a simple and reliable way.]_
- Postpone:  Ideally the updater could have a way to run an embedded script in the newly installed version, to do any needed finalization.  But there isn't really a way to reliably determine whether it needs to run, nor to handle errors that may occur.  And a more reliable mechanism is to do upgrade steps on the next inject.
- Include `wildmatch()` and an `fnmatch()` wrapper for it.  But should first update it to support UTF8.  _[Not worth the effort; and recursive match generation doesn't make much sense anyway.  Could potentially be useful in globbing purposes other than match generation, though.]_
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
