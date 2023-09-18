_This todo list describes ChrisAnt996's current intended roadmap for Clink's future.  It is a living document and does not convey any guarantee about when or whether any item may be implemented._

<br/>

# IMPROVEMENTS

## High Priority
- `Ctrl-Space` then `dw` highlights **THREE** characters but should only highlight **TWO**.
- A reliable way for scripts to tell whether they're loaded in `clink set` versus in `cmd`.
- Add link to File Locations in the Saved Command History section.

## Normal Priority
- Open issue in Terminal repo about bugs in the new shell integration in v1.18.
  - Transient prompt can lead to Terminal getting confused about where prompt markers are.
  - Can the same thing happen with zsh and powerlevel10k transient prompt?
  - Provide a sample .txt file that repros the issue.  Maybe multiple .txt files that chain together (or with a pause; is there an escape code for a pause?) to show the UX flow.

## Low Priority
- Color improvements:
  - Color themes.  Some way to import color settings en masse.  Some way to export color settings as well?
  - More sophisticated match color definitions?
    - Unify match color settings, e.g. something like `%LS_COLORS%` or `%COLORDIR%` (from 4Dos/4NT/TakeCommand).  The fractured `%LS_COLORS%` + `color.readonly` stuff is awkward and confusing.
    - Ability to combine conditions, e.g. executable=1, readonly=32, executable _AND_ readonly=1;32.
      - Spaces should mean an "or" operator, instead of an "and" operator (like in 4Dos/4NT/TakeCommand etc).
      - Precedence for "or", "and", "xor" can be simply left to right (like in 4Dos/4NT/TakeCommand etc).
      - Automatically optimize rule evaluation by processing CFLAG checks before pattern checks in any group of "or" clauses.
      - If parsing is fast enough, then maybe don't even bother "compiling" the rules, and simply parse for every entry?
- The `:` and `=` parsing has a side effect that flags like `-f`_`file`_ are ambiguous: since parsing happens independently from argmatchers, `-fc:\file` could be `-f` and `c:\file` or it could be `-fc:` and `\file`.
  - Revisit the possibility of allowing `line_state` to be mutable and argmatchers adjusting it as they parse the input line?  _No; too messy.  E.g. splitting `"-fc:\foo bar"` gets weird because quoting encloses **two adjacent** words._
  - But an important benefit of the current implementation is that `program_with_no_argmatcher --unknown-flag:filename` is able to do filename completion on `filename`.
  - Maybe a better solution would be to let argmatchers specify `getopt`-like parsing rules.  Then an argmatcher parser could split the word into `-f` and `c:\file`, and the second part could be put into a "pending word" variable which the parser could check before trying to advance the parser's word index?  And could even potentially recognize `-az` as two valid flags `-a` and `-z` when appropriate (and if either flag is unknown, then color the whole word as unknown).
- Allow removing event handlers, e.g. `clink.onbeginedit(func)` to add an event handler, and something like `clink.onbeginedit(func, false)` or `clink.removebeginedit(func)` to remove one?  Or maybe return a function that can be called to remove it, e.g. like below (but make sure repeated calls become no-ops).  The `clink-diagnostics` command would need to still show any removed event handlers until the next beginedit.  But it gets tricky if `func` is already registered -- should the new redundant registration's removal function be able to remove the pre-existing event handler?
    ```
    local remove = clink.onbeginedit(func) -- add func
    remove()                               -- remove func
    ```
- Allow Lua to set the comment row for the input line?
  - Need a simple and reliable trigger for clearing the comment row later; maybe `clink.onaftercommand()` is enough?
  - Don't add this ability unless there is a way to ensure comment rows don't get "leaked" and continue showing up past when they were relevant.
- Make a reusable wrapper mechanism to create coroutine-friendly threaded async operations in Lua?

## Follow Up
- Push update to z.lua repo.

## Argmatcher syntax
- See the argmatcher_syntax branch.

<br/>
<br/>

# "New" commits from Martin

## To Be Considered
- Ctrl-W changes.  While I agree in principle, this kind of change upsets people who are used to bash.  Maybe it should only apply when `clink.default_bindings` == `windows`?
  - [Ctrl-W is more useful if it kills on more granular word boundaries](https://github.com/mridgers/clink/commit/5ee004074e0869273ac42006edef4bcdcfd0e24f)
  - [Smarter Ctrl-W word deletion](https://github.com/mridgers/clink/commit/a385a1695bb425d6f48aae4e587c9c06af8515f6)
- [Type name style change](https://github.com/mridgers/clink/commit/e6baa31badb8854413dd34988cc33b7aeb68b7e0) -- Huge; renames types from `foo_bar` to `FooBar`.

## Leaning Towards No
- [Changed member style](https://github.com/mridgers/clink/commit/fd5041a34ba162fd3adc1b7b0c5910438e343235) -- Huge; renames members from `m_foo` to `_foo`.
- [Use AppData/Local for a the DLL cache as temp can get cleaned](https://github.com/mridgers/clink/commit/8ed3cb0b427970c8082acb238071b26d5e788057) -- Isn't getting cleaned desirable?  Otherwise DLL versions accumulate without bound.
- It could be reasonable to add an iterator version of `os.globfiles()`, but replacing it breaks compatibility.
  - [Made Lua's os.glob*() work like an iterator instead of building a table](https://github.com/mridgers/clink/commit/13fc3b68046d2cee0f2188b9c8d54fa0cbc18718)
  - [os.glob*() tests](https://github.com/mridgers/clink/commit/5cfacee2a2b8230968854bc94bc3e1adf6b56bf9)
  - [Fixed "cd \\" Lua error](https://github.com/mridgers/clink/commit/d2ffed58f75597cec08d85e8abf4fafc0b60a067)
  - [builder::addmatches() now also accepts a function](https://github.com/mridgers/clink/commit/6a2b818efd84377b3a625bb1ecdeffe89da20cd6) -- This is inconsistent with `argmatcher:addflags()` and `argmatcher:addarg()`, and is generally non-intuitive.

## No
- [Don't expect the user to account for a null terminator](https://github.com/mridgers/clink/commit/4583281d464933d9ce021aedcdf3edc5e3fdc189) -- This still requires the user to account for a null terminator, by removing the space for the null terminator, otherwise the block gets sized differently than expected (and can have extra slop allocated).
- Removing all copyright dates seems problematic.  Isn't it required in copyright notices?  And in the program logo header it provides date context for the program version being used.
  - [Removed date from header](https://github.com/mridgers/clink/commit/7ca14e8d4c82b4a6e6801af4b702329d8de29eef)
  - [Removed years](https://github.com/mridgers/clink/commit/b732e873fc337671fabc62659a0a578cf617028c)
- [Hand-rolled remote-call thunks. Previous approach was assuming that the compiler won't do what it eventually did; add complex prologue/epilogue.](https://github.com/mridgers/clink/commit/76aee60e5cdad911a0b478765499f8fbdd848619) -- That was resolved in chrisant996/clink by turning off certain compiler features in the relevant files (see commits 462a985e66, 7ba05ea77e, e0750b173d, 03320a2069, and 3dd4f49e72), which also makes ARM64 work without special custom assembly.
- [Only log inputrc information once](https://github.com/mridgers/clink/commit/f2228b9d64e30852f415969f5a0409e252df3c01) -- That was only annoying because Lua was recreated on each prompt, which had to be removed because (1) it broke `z`, (2) it bogs down performance, and (3) chrisant996/clink uses coroutines which is incompatible with recreate Lua on each prompt.  The logging change would also miss when the user makes a configuration change that results in a different inputrc file getting loaded.
- `io.popen2()` -- Why was this done?  If this is just trying to support UTF8, then chrisant996/clink solved that by setting the C runtime locale to `".utf8"`.
  - [Added io.popen2() that directly uses Windows' API](https://github.com/mridgers/clink/commit/bd69fe219501e050dd1b92c13fd9b842c497885d)
  - [Make sure there's a valid stderr handle](https://github.com/mridgers/clink/commit/795c371cfc0cf00888322d68791c99f670210bfe)
  - [Use the parent process' console](https://github.com/mridgers/clink/commit/2c505bd29c2c9493836e6b2bce29a2cc4e88182b)
  - [Up-values will always exist so there's no need for a null check](https://github.com/mridgers/clink/commit/8dcb97025af170afe5f357c6ac2ff7101a758b3d)
  - [Put io.popen2() processes in a different Ctrl-C group](https://github.com/mridgers/clink/commit/6bd9d2c4ca346adfad8ca413776f417cadfca693)
  - [Corrected a spelling mistake](https://github.com/mridgers/clink/commit/d0a6e8708032c3f08a776953cc59c90e660bc684)
- [Better implementation of non-ASCII compatible lua_state::do_file()](https://github.com/mridgers/clink/commit/c7105d12a9c35b45d2eef7760df323317bb15d87) -- This was solved in chrisant996/clink by setting the C runtime locale to `".utf8"`.
- Jmp hooking -- Not needed in chrisant996/clink because (1) Detours is available and (2) IAT hooking is used exclusively.
  - [Very simple and incomplete x86/x64 length disassembler](https://github.com/mridgers/clink/commit/2355aafd2914f2e7af997ae75eac2d9cc3aaa313)
  - [New jmp-style hooking mechanism that is Win11 compatible](https://github.com/mridgers/clink/commit/7ed4c8f0215c45e96f757dd2ea9d4e44b689cf58)
  - [Branch displacement was back-to-front](https://github.com/mridgers/clink/commit/3047b9b91e75131db0243bd6cde4a36fffe42b92)
- [Use Windows 10's virtual terminal if available](https://github.com/mridgers/clink/commit/530196af81f9981d18888e1326ff37d0bd249d7e) -- SetConsoleMode does not validate flags, so the approach here will not detect when `ENABLE_VIRTUAL_TERMINAL_PROCESSING` is not available.  This was solved in chrisant996/clink by checking the OS version and the ConsoleV2 regkey and etc.
- [unix-filename-rubout uses forward slashes too](https://github.com/mridgers/clink/commit/d82ad89cb0e353ece72f0ddf399632ca21fdcd5c) -- This was solved in chrisant996/clink by refactoring how Readline handles path separators, and the changes were ratified by Chet Ramey and incorporated into the official Readline distribution.
- [Let's try a different default colour scheme (white on red was too angry)](https://github.com/mridgers/clink/commit/dd5aeb00b1fed954ec12af8e76598e4c74453b88) -- The color scheme in chrisant996/clink already made similarly-motivated changes.
- [Option to remap ESC to; raw, ctrl-c, or revert-line (line Windows)](https://github.com/mridgers/clink/commit/295a9e4a3628e94b8b889286ae96c9355dc0ad77) -- Conflicts with `terminal.raw_esc` and the chrisant996/clink approach for differentiating <kbd>Esc</kbd> input from the `ESC` character, which allows binding <kbd>Esc</kbd> to anything the user wishes.

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
- Once in a while raw mouse input sequences spuriously show up in the edit line; have only noticed it when the CMD window did not have focus at the time.  _[Not fixed by bb870fc494.]_ _[Have not seen for many weeks.]_ _[Likely due to `ENABLE_VIRTUAL_TERMINAL_INPUT` and largely mitigated by a8d80b752a.]_
- Mouse input toggling is unreliable in Windows Terminal, and sometimes ends up disallowing mouse input.  _[Might be fixed by bb870fc494?]_
- `"qq": "QQ"` in `.inputrc`, and then type `qa` --> infinite loop.  _[Was occurring in a 1.3.9 development build; but no longer repros in a later 1.3.9 build, and also does not repro in the 1.3.8 release build.]_
- Windows 10.0.19042.630 seems to have problems when using WriteConsoleW with ANSI escape codes in a powerline prompt in a git repo.  But Windows 10.0.19041.630 doesn't.
- Windows Terminal crashes on exit after `clink inject`.  The current release version was crashing (1.6.10571.0).  Older versions don't crash, and a locally built version from the terminal repo's HEAD doesn't crash.  I think the crash is probably a bug in Windows Terminal, not related to Clink.  And after I built it locally, then it stopped crashing with 1.6.10571.0 as well.  Mysterious...

## Punt
- Provide some kind of "line editor tester" in the `clink lua` interpreter to facilitate writing unit tests for argmatchers?  _[No.  Too many fundamental incompatibilities with the rest of the code.  Completion script authors can do unit testing of their own code, but trying to do end-to-end testing of Clink itself from within Clink itself with being integrated with CMD?  Hard no.]_
- Issue #387 is a request to add an option to put the cursor at the end of the search text when using `history-substring-search-backward` (and `-forward`).  But that diverges even more from Readline, and I'm actively trying to instead converge as much as possible.  _[The request should be made against bash/Readline.  If it gets implemented there, Clink will be able to pick up the change.]_
- Consider not redrawing while resizing the terminal, if there is no RPROMPT?  Maybe just flag that a full redraw needs to happen, and defer it until the next time a redraw is normally requested?  _[Defer any further changes to terminal resize behavior until there is further feedback.]_
- Ctrl-Break does not interrupt Lua scripts during `onendline` or `onfilterinput`.  The Lua engine doesn't support being interrupted; it only supports the application being terminated.  The engine could be modified to check for a `volatile` flag, but that would need to be done carefully to ensure it doesn't interrupt Clink's own internal Lua scripts.  _[Not needed; it's very rare, and hooking up `os.issignaled()` should be sufficient, though it does require scripts to explicitly support being interrupted.]_
- Some way for `history.save false` to not do any disk IO for history, but still enable `clink history` to show the session's history (probably using Shared Memory).  _[Unfavorable cost vs benefit; expensive and complicated, while offering very little benefit beyond what could be achieved by simply applying ACLs and/or encryption to the profile directory, which is something that is best done externally from Clink.]_
- Coroutines can call `clink.refilterprompt()` and it immediately refilters while in the coroutine.  Should it instead set a flag to refilter after the coroutines have yielded?  _[It should be fine because only `line_editor_impl` has an input idle callback that runs Lua coroutines.]_
- Show time stamps in history popup?  _[Gets complicated because of horizontal scrolling.  Too many edge cases; the benefit is not worth the cost.]_
- Some way to push keys?  (Push keys to Clink; not to other processes.)  _[Can use `WScript.Shell.SendKeys` when that's needed.]_
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
