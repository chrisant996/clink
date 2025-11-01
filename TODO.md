_This todo list describes ChrisAnt996's current intended roadmap for Clink's future.  It is a living document and does not convey any guarantee about when or whether any item may be implemented._

<br/>

# IMPROVEMENTS

## Mystery Issue

## High Priority

## Normal Priority
- Add Lua APIs for:
  - Web requests.  So that update.lua can stop using powershell `Invoke-WebRequest`.  Use WinHttp.
  - Unzip.  So that update.lua can stop using powershell for unzipping.  Use IShellDispatch.
- Cannot detect Windows Terminal when conhost automatically launches Windows Terminal (the "let Windows decide" setting).
- Completion sometimes doesn't work.  In the rare cases where I've experienced this, there were no matches at all.
  - ~~Maybe the repro is to queue up typing before the prompt, so that when the prompt shows it starts a coroutine to generate matches (e.g. for suggestions) but then typing and `TAB` is processed while the matches coroutine is already running?~~
  - I tried forcing several different race conditions, and none of them could reproduce the issue.  It happens only very rarely, so until I can find more detailed context, I can't even tell if it's a recent regression or if it only occurs in a certain configuration.  But my guess is it's either a recent regression, or an issue exposed/exacerbated by recent features.
- Windows 11 build 26100 supposedly has surrogate pair support (and emoji support) in the conhost terminal:  use the `wcwidth-verifier` project to generate updated metrics for Windows 11 build 26100 and higher.
  - It sort of has surrogate pair support, but the console thinks most are width 1 even though they render as wider than width 1, so it doesn't seem right/ready yet.
  - Terminal 1.22 and 1.24 Preview have a bunch of glyphs that render as different widths;
- `ecma48_terminal_out::build_pending` looks like it might not quite handle UTF8 decoding correctly, especially in cases of invalid UTF8.
- Review the REVIEW: comments about always/sometimes/never leaking an undo list.

## Low Priority
- Some way for `io.popen`, `io.popenyield`, `os.execute`, etc to run without a console window.  `clink.execute` exists, but has quirks and doesn't support yielding.
- On Windows 8.1, running `clink set debug.log_terminal true` causes CMD to crash.  It seems that the detour for `WriteFile` is bad, which causes `fclose` on the log file to crash when it tries to call `WriteFile` to flush the pending output.
- Find a high performance way to detect git bare repos and encapsulate it into a Lua function?
- Event handler enhancements:
  - Allow setting an optional `priority` when registering event handlers?  So that scripts can control the precedence of `onbeginedit`, `onendedit`, and so on.
  - Allow adding a ONE-TIME event handler which automatically removes itself upon firing?  And `clink-diagnostics` would need to show any ONE-TIME event handlers until the next beginedit.
    - Watch out for back-compat:  Consider making _new API functions_ for adding one-time event handlers.  Adding an optional parameter is dangerous because a script author could use it without taking steps to ensure backward compatibility, and then potentially significant malfunctions could occur.  And anyway, probably only a small number of events would actually need support for one-time handlers (maybe even only `onbeginedit`).
- Consider plumbing `lua_State*` through all layers to help guarantee things don't accidentally cross from a coroutine into main?
- Make a reusable wrapper mechanism to create coroutine-friendly threaded async operations in Lua?

## Extra Low Priority
- Option for the installer to add the Clink directory to the system PATH?  **WARNING:**  The main problem is about All Users...
- I'd love to let the installer run without Admin privilege, but I don't see how to do that safely because of the need for updating All Users AutoRun.
- Windows Terminal now has 3 ways of measuring character widths.  There's a DECRQM escape code `\x1b[?2027$p` to ask which mode is being used.  The wcwidth updates in Clink are consistent with the "Grapheme clusters" mode.  If the "wcswidth" and "Windows Console" modes turn out to be worth supporting, then that would require a bunch of extra work and configuration.
- Open issue in Terminal repo about bugs in the new shell integration in v1.18.
  - Transient prompt can lead to Terminal getting confused about where prompt markers are.
  - Can the same thing happen with zsh and powerlevel10k transient prompt?
  - Provide a sample .txt file that repros the issue.  Maybe multiple .txt files that chain together (or with a pause; is there an escape code for a pause?) to show the UX flow.

## Argmatcher syntax
- See the argmatcher_syntax branch.

<br/>
<br/>

# NOTES TO SELF

## Renewing Certum Code Signing Certificate

- For the Open Source cert, there isn't an option to purchase a renewal, instead you purchase a new certificate which ends up acting like a renewal.
- The new certificate expiration starts when it's purchased and activated, NOT when the old certificate expires.
- After activating the new certificate, it must be installed onto the card via the Install Certificate button in the web page for the cert.
- The certificates are in the Common profile on the card.
- After the new cert is installed, the old cert must be removed from the card, otherwise SignTool can't figure out to ignore the expired cert.
- The Remove button is normally hidden, but in the Settings dialog there is an option to show the Remove button in the Common profile.

<br/>
<br/>

# "New" commits from Martin

## To Be Considered
- Ctrl-W changes.  While I agree in principle, this kind of change upsets people who are used to bash.  Maybe it should only apply when `clink.default_bindings` == `windows`?
  - [Ctrl-W is more useful if it kills on more granular word boundaries](https://github.com/mridgers/clink/commit/5ee004074e0869273ac42006edef4bcdcfd0e24f)
  - [Smarter Ctrl-W word deletion](https://github.com/mridgers/clink/commit/a385a1695bb425d6f48aae4e587c9c06af8515f6)
- [Type name style change](https://github.com/mridgers/clink/commit/e6baa31badb8854413dd34988cc33b7aeb68b7e0) -- Huge; renames types from `foo_bar` to `FooBar`.

## No
- [Changed member style](https://github.com/mridgers/clink/commit/fd5041a34ba162fd3adc1b7b0c5910438e343235) -- Huge; renames members from `m_foo` to `_foo`.  And what about `c_` and `s_` and `g_`?  Keeping `m_` seems useful, and avoids a huge amount of churn.
- It could be reasonable to add an iterator version of `os.globfiles()`, but replacing it breaks compatibility -- The way these commits implemented it is problematic because it relies exclusively on garbage collection to release the OS FindFirstFile handle, and that can create sharing violations which the Lua script cannot fix except by forcing garbage collection.  But something similar to `io.open()` and `:lines()` and `:close()` would be fine, and would be consistent with `opendir()`, `readdir()`, and `closedir()`.
  - [Made Lua's os.glob*() work like an iterator instead of building a table](https://github.com/mridgers/clink/commit/13fc3b68046d2cee0f2188b9c8d54fa0cbc18718)
  - [os.glob*() tests](https://github.com/mridgers/clink/commit/5cfacee2a2b8230968854bc94bc3e1adf6b56bf9)
  - [Fixed "cd \\" Lua error](https://github.com/mridgers/clink/commit/d2ffed58f75597cec08d85e8abf4fafc0b60a067)
  - [builder::addmatches() now also accepts a function](https://github.com/mridgers/clink/commit/6a2b818efd84377b3a625bb1ecdeffe89da20cd6) -- This is inconsistent with `argmatcher:addflags()` and `argmatcher:addarg()`, and is generally non-intuitive.  Also, beware -- it has a significant bug:  when given a table or function, it returns 2 _without actually pushing 2 args_, which corrupts the stack and will cause failures.
- [Use AppData/Local for a the DLL cache as temp can get cleaned](https://github.com/mridgers/clink/commit/8ed3cb0b427970c8082acb238071b26d5e788057) -- Getting cleaned is desirable.  Otherwise DLL versions accumulate without bound.
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
- [unix-filename-rubout uses forward slashes too](https://github.com/mridgers/clink/commit/d82ad89cb0e353ece72f0ddf399632ca21fdcd5c) -- This was solved in chrisant996/clink by refactoring how Readline handles path separators, and the changes were ratified by Chet Ramey and incorporated into the official Readline distribution (although subsequent changes in Readline haven't followed the refactored approach and will need further cleanup).
- [Let's try a different default colour scheme (white on red was too angry)](https://github.com/mridgers/clink/commit/dd5aeb00b1fed954ec12af8e76598e4c74453b88) -- The color scheme in chrisant996/clink already made similarly-motivated changes.
- [Option to remap ESC to; raw, ctrl-c, or revert-line (line Windows)](https://github.com/mridgers/clink/commit/295a9e4a3628e94b8b889286ae96c9355dc0ad77) -- Conflicts with `terminal.raw_esc` and the chrisant996/clink approach for differentiating <kbd>Esc</kbd> input from the `ESC` character, which allows binding <kbd>Esc</kbd> to anything the user wishes.

<br/>
<br/>

# APPENDICES

## Known Issues
- When a process (such as `git` or many terminal apps ported from Unix/Linux) set the `ENABLE_VIRTUAL_TERMINAL_INPUT` console input mode, then any special input keys typed before Clink regains control will be pre-translated into VT input sequences, which will interfere with Clink's keyboard driver.  This typically ends up looking like the input line gets cleared and has just `[A` or `[B` or similar text, instead of what was typed by the user.  This happens because the OS translates the input into VT sequences at the moment the keys are pressed, instead of waiting until the input is read by an application.  So, if the console input mode changes after the input is typed but before Clink reads the input, then the input does not match the input format requested by the console input mode.  The only way to work around this is to set the `terminal.raw_esc` setting to `true` **and** only use key bindings in Clink that the OS is able to generate on its own (which means no `Ctrl-Space`, no `Ctrl-Shift-Letter`, and various other key combinations that Clink recognizes but which Unix/Linux do not recognize).
- When `echo` is `off`, CMD doesn't print a prompt, and Clink can't which ReadConsoleW calls are for reading the input line.  In theory, Clink could use `RtlCaptureStackBackTrace()` to deduce when a call is for the input line (see comment in `host_cmd::read_console()`), but that API isn't reliable for use in non-debug code.
- `foo bar a/b/c` will try to expand `a/b/c` as an abbreviated path even if `foo bar` never generates filename matches.  This is a case that Clink can't really get perfectly right anymore, because of the automatic deduction of whether to use file matches.  Overall, this seems acceptable.
- Readline's `expand_tilde()` doesn't handle embedded `{space}{tilde}{pathsep}` correctly in strings; `rl.expandtilde()` does, and has an optional parameter to use Readline's original style of tilde expansion.
- Cursor style may behave unexpectedly in a new console window launched from a Windows Terminal console, or in a console window that gets attached to Windows Terminal.  This is because there's no reliable way for Clink to know whether it is running inside Windows Terminal.  Related to [Terminal #7434](https://github.com/microsoft/terminal/issues/7434).
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).
- [#369](https://github.com/chrisant996/clink/issues/369) and [mridgers#531](https://github.com/mridgers/clink/issues/531) anti-malware suites sometimes think Clink is malicious _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There's no way around that.  Signing the binaries might reduce that, but that's financially expensive and there's no way for an indepedent author to get an EV code signing certificate even if they were willing to pay the thousands of dollars per year.]_
- [Terminal #10191](https://github.com/microsoft/terminal/issues/10191#issuecomment-897345862) Microsoft Terminal does not allow a console application to know about or access the scrollback history, nor to scroll the screen.  It blocks Clink's scrolling commands, and also the `console.findline()` function and everything else that relies on access to the scrollback history.
- The auto-updater settings are stored in the profile.  That makes it more cumbersome to control the auto-updater settings if you use multiple Clink profiles.  However, it makes it possible to control the auto-updater settings separately in "portable installs" (e.g. on a USB memory stick).
- Lua code can check if there is real console input available, and can read real console input.  But there is no way for Lua code to check whether there is any input queued for Readline (pending input, pushed input, macro text).  That probably makes sense, since there is (correctly) no way for Lua code to read input queued for Readline.
- Readline history bug:  With `UP` bound to `history-search-backward` --> `UP` to a history line with 2 or more characters, `Backspace` until 1 character remains, `UP` a few times, `Home`, `Right`, `UP`, `Ctrl-Z` --> The history is left in a state where the original history line is lost until the history file is reloaded; it loses its undo list (and thus also its modmark).

## Mystery
- Randomly hit `assert(group == m_prev_group || group == m_catch_group);` upon `Ctrl-Space`.  It left input in a weird state with `clink-select-complete` still active but not handling input.  Could not repro again after I got out of the state.  It seems likely to be a long-standing issue in some obscure edge case.
- Mouse input toggling is unreliable in Windows Terminal, and sometimes ends up disallowing mouse input.  _[Might be fixed by [bb870fc494](https://github.com/chrisant996/clink/commit/bb870fc49472a64bc1ea9194fe941a4948362d30)?]_
- `"qq": "QQ"` in `.inputrc`, and then type `qa` --> infinite loop.  _[Was occurring in a 1.3.9 development build; but no longer repros in a later 1.3.9 build, and also does not repro in the 1.3.8 release build.]_

## Punt
- Some wizard for interactively binding/unbinding keys and changing init file settings; can write back to the .inputrc file.  _[This is something that fits better in a script, perhaps in clink-gizmos, if at all.]_
- line_state parsed `foo^ bar` as a single word "foo^ bar", but CMD parses it as two words "foo" and "bar".  The parser is fixed now, but what about downstream edge cases where things check the next character after a word (or try to skip a run of spaces but get confused by `foo ^ ^ bar`)?  _[Not worth spending time on this outlier edge case until/unless someone can present a compelling situation.]_
- Once in a while raw mouse input sequences spuriously show up in the edit line; have only noticed it when the CMD window did not have focus at the time.  _[Not fixed by [bb870fc494](https://github.com/chrisant996/clink/commit/bb870fc49472a64bc1ea9194fe941a4948362d30).]_ _[Have not seen for many weeks.]_ _[Likely due to `ENABLE_VIRTUAL_TERMINAL_INPUT` and largely mitigated by [a8d80b752a](https://github.com/chrisant996/clink/commit/a8d80b752a3c4ff8660debeec0133a009fb04051).]_ _[Root cause is https://github.com/microsoft/terminal/issues/15711]_
  - It continues to happen.  Repro is something like this:  run some command like `git pull`; while it's running type a few letters and then the UP key; when the command finishes the input line will contain `[A`.
  - It might be that 15711 isn't fixed yet, or it could be an inherent problem in toggling `ENABLE_VIRTUAL_TERMINAL_INPUT`.  The console subsystem seems to process input before it's actually read.  If that's indeed what's happening, then that is probably too big of a change for the OS to make (e.g. risk of regression for breaking app scenarios that currently rely on that behavior to work).
  - **CONFIRMED:**  The root cause is that while the console input mode includes `ENABLE_VIRTUAL_TERMINAL_INPUT` then the console immediately converts input into VT input, even before the input is read.  This means when processes set `ENABLE_VIRTUAL_TERMINAL_INPUT` (such as `git`) then any arrow keys/etc that are before CMD/Clink regains control are received by Clink as VT input sequences, even though Clink clears the `ENABLE_VIRTUAL_TERMINAL_INPUT` mode before reading any input.  Even setting `terminal.raw_esc` true can't fully mitigate this OS issue, because the OS doesn't generate the same VT input sequences as Clink does (for example `Ctrl-Shift-B` or `Ctrl-Space` or etc).
  - _[The only way to overcome that is to set `terminal.raw_esc` true and only use key bindings in Clink which the OS is able to generate on its own.]_
- [#731](https://github.com/chrisant996/clink/issues/731); `where [a` completion tries to complete `a` instead of `[a` -- explore possibilities for how to make completion more intuitive, while still conforming to cmd.exe parsing and completion rules.  _[No; the small edge case benefit is not worth the complexity and cost.  It's more complicated than just word break characters -- for example `rem[`<kbd>Tab</kbd> cannot simply treat `[` as part of a match because it actually has syntactic meaning for the `rem` command.  If `[` is treated as part of the argument, then it expands to `rem"[abc]" ` and CMD interprets it as an attempt to launch a program by that name, **NOT** as the `rem` command followed by some text.  Yes, it could be possible to add special case logic to for example forcibly insert a space before the completion, but that is not universally reasonable and there isn't a clear set of rules for when to insert a space or not.  I thought of some other approaches as well, but all of them are ambiguous and/or conflict with some other bit of CMD syntax.  If the original poster wants to lobby for a feature change here, they will need to either submit a pull request for review or submit a feature specification document for review.]_
- Investigate adding a line at the top of clink_settings (#729) such as "# This is an auto-generated file. Certain kinds of manual edits are not preserved."
  - Watch out for edge cases that could potentially result in duplication of the line (especially if the first setting is from a script that is no longer loaded, so it _and any preceding comments_ get moved to the tail section of the file). _CONFIRMED: multiple cases exist where an older version of Clink can lead to unbounded growth in the clink_settings file if such a line is added._
  - Watch out for edge cases where an older version of Clink reads and re-writes the settings file; what will happen...? _CONFIRMED: multiple cases exist where an older version of Clink can lead to unbounded growth in the clink_settings file if such a line is added._
  - Look for other possible problems that could result from the seemingly simple suggestion.
- Some wizard for interactively viewing/modifying color settings.  _[This is low priority now that Clink supports .clinktheme color themes.]_
- Can `git.getstatus()` be simplified even further, so it automatically handles `clink.promptcoroutine()`?  Maybe a `git.getstatusasync()` function?  _[Not at this time:  that could be something to consider for a bunch of various APIs later, but for now scripts should just use the normal `clink.promptcoroutine()` usage pattern.]_
- Input hinter; need some way for `:gethint()` to work with coroutines and override the optimization and call it again.  _[No:  that would lead to hints cycling through multiple values at the same cursor position.  Once a hint is shown, it shouldn't change until at least another keypress occurs.]_
- `^>nul echo hello` behaves strangely:  It redirects to `echo` and tries to run `hello`.  What is going on with that syntax?  Any `^` combined with redirection before the command word seems to go awry one way or another.  It looks like a bug in the CMD parser.  _Trying to accurately predict how the bug will behave in all possible contexts seems unrealistic._
  - `^>xyz` occurring before the command word loses the `xyz` and pulls the _next_ token as the redirection target.
  - `^2>&1 whatever` fails to duplicate a handle.
  - `2^>&1 whatever` says `&` was unexpected.
  - Etc.
- [#554](https://github.com/chrisant996/clink/issues/554) Consider adding some way to configure Clink to try to run as a "portable program" in the sense that it doesn't write to any OS default locations (such as %TEMP%) and instead writes only to places that are specifically configured.  But Lua scripts and programs they launch would need to also have their own special "portable program" support to avoid writing to OS default locations (especially %TEMP%).  And what size of temporary files are ok to redirect to a "portable storage"?  And who maintains/purges files from the portable storage?  Because Clink runs scripts and other programs, trying to support a "portable program" mode is more complicated than it might sound at first.  _[No; it's not feasible.]_
- Clink's `win_terminal_in` keyboard driver generates some things differently than VT220:
  - Ideally it might have mapped `CTRL-SPC`->0x00(`^@`), `CTRL--`->0x0d(`^M`), `CTRL-/`->0x1f(`^_`), `CTRL-?`->0x7f(`^?` aka `Rubout`).  But I think Clink's approach is overall better for those keys.
  - Ubuntu in Windows Terminal receives `^?` for `BACKSPC` and `^H` for `CTRL-BACKSPC`.  But that seems backwards versus what I've always seen on many systems over the decades, so I think Clink should stick with `^H` for `BACKSPC` and `^?` for `CTRL-BACKSPC`.
  - The `CTRL-DIGIT` keys intentionally produce unique key sequences instead of `CTRL-0` through `CTRL-9` mapping to certain control codes or `1`, `9`, and `0`.
- [x] The `:` and `=` parsing has a side effect that flags like `-f`_`file`_ are ambiguous: since parsing happens independently from argmatchers, `-fc:\file` could be `-f` and `c:\file` or it could be `-fc:` and `\file`.  _[Too much complexity for too little benefit too rarely.]_
  - Revisit the possibility of allowing `line_state` to be mutable and argmatchers adjusting it as they parse the input line?  _No; too messy.  E.g. splitting `"-fc:\foo bar"` gets weird because quoting encloses **two adjacent** words._
  - But an important benefit of the current implementation is that `program_with_no_argmatcher --unknown-flag:filename` is able to do filename completion on `filename`.
  - Maybe a better solution would be to let argmatchers specify `getopt`-like parsing rules.  Then an argmatcher parser could split the word into `-f` and `c:\file`, and the second part could be put into a "pending word" variable which the parser could check before trying to advance the parser's word index?  And could even potentially recognize `-az` as two valid flags `-a` and `-z` when appropriate (and if either flag is unknown, then color the whole word as unknown).
  - _**UPDATE:** This is handled now by [`arghelper.lua`](https://github.com/vladimir-kotikov/clink-completions/commit/db9ed236233b8cb81b8874acf84453de2749fd70) via `concat_one_letter_flags=true`._
- Maybe consider a way to allow piping matches and help into `less` or other external pager?  It would break/interfere with the intended flow and behavior of the `complete` command and it might not work with ANSI codes.  But in [clink-completions#500](https://github.com/vladimir-kotikov/clink-completions/issues/178) a user cited a link where someone assumed that bash would use an external pager like `less`.  _[No; it would break how completion is designed to work, and ANSI escape codes couldn't be used.]_
- CMD sets `=ExitCode` = the exit code from running a program.  But it doesn't set the envvar for various other things that update CMD's internal exit code variable.  So, Clink's tempfile dance to get `%ERRORLEVEL%` is still necessary.
- A reliable way for scripts to tell whether they're loaded in `clink set` versus in `cmd`.  _[No.  The only case reported that needed this was trying to access key bindings when the script was loaded, and due to a bug in `rl.getkeybindings()` Clink crashed.  The crash has been fixed (now it returns an empty table instead), and the script is better implemented using `clink.oninject()` anyway.]_
- Provide some kind of "line editor tester" in the `clink lua` interpreter to facilitate writing unit tests for argmatchers?  _[No.  Too many fundamental incompatibilities with the rest of the code.  Completion script authors can do unit testing of their own code, but trying to do end-to-end testing of Clink itself from within Clink itself with being integrated with CMD?  Hard no.]_
- [#387](https://github.com/chrisant996/clink/issues/387) requests adding an option to put the cursor at the end of the search text when using `history-substring-search-backward` (and `-forward`).  But that diverges even more from Readline, and I'm actively trying to instead converge as much as possible.  _[The request should be made against bash/Readline.  If it gets implemented there, Clink will be able to pick up the change.]_
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
- [x] Readline 8.1 has slight bug in `update_line`; type `c` then `l`, and it now identifies **2** chars (`cl`) as needing to be displayed; seems like the diff routine has a bug with respect to the new faces capability; it used to only identify `l` as needing to be displayed.  _[Moot; Clink no longer uses Readline's display implementation.]_
- Optional feature to simplify auto-path-separator after completion, like in `zsh`:  highlight `\` in a color, and if <kbd>Space</kbd> is the next input then replace the `"\"` with `" "`.  _[Not worth it; there is very little value, and there are many side effects, e.g. wrt autosuggest.  I got excited at first, but then I realized what I really need is a better way to signal for `menu-complete` to accept a directory it's inserted and start a new completion.  And then I realized, since completions are normalized as of commit 9ec9eb1b69 in v1.1.24, typing <kbd>&bsol;</kbd><kbd>Tab</kbd> goes from `foo\` to `foo\\` to `foo\bar`, so the scenario I had in mind is already fully solved in a simple and reliable way.]_
- Postpone:  Ideally the updater could have a way to run an embedded script in the newly installed version, to do any needed finalization.  But there isn't really a way to reliably determine whether it needs to run, nor to handle errors that may occur.  And a more reliable mechanism is to do upgrade steps on the next inject.
- There's no straightforward way to let Lua scripts change the current directory and have CMD pick up the changed state.  CMD maintains internal private state about what directory to use when running commands and programs.  Running `cd` is the only way to alter CMD's internal private state.
- Make `clink-show-help` call out prefix key sequences, since they can behave in a confusing manner?  _[Complex present in a non-confusing way, and very rare to actually occur.  Not worth the investment at this time.]_
- Maybe deal with timeouts in keyboard input?  Could differentiate <kbd>Esc</kbd> versus <kbd>Esc</kbd> <kbd>Esc</kbd> but is very dangerous because it makes input processing unpredictable depending on the CPU availability, and macro recording/replay.  _[Too dangerous.  And turned out to not be the issue.]_
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
- [x] A way to disable/enable clink once injected.  _[Why?]_ _[Available via [noclink.cmd](https://github.com/chrisant996/clink-gizmos/commit/5126152d4ea98bf2d4470d13a34f8f88645a29d5) in [clink-gizmos](https://github.com/chrisant996/clink-gizmos)]_
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[Unrelated to Clink; the exact same behavior occurs with plain cmd.exe]_

---
Chris Antos - sparrowhawk996@gmail.com
