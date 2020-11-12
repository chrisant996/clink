ChrisAnt Plans

<br/>

# Internal Work

- Promote store_impl to be a template and public, so the popup list can use it?

<br/>
<br/>

# BETA

- Document the `clink` commands (autorun, inject, etc).

## Cmder, Powerline, Clink-Completions
- Update clink-completions to have better 0.4.9 implementations, and also to conditionally use the new API when available.
- Update clink-git-extensions to have better 0.4.9 implementations, and also to conditionally use the new API when available.
- Update cmder-powerline-prompt to work outside of cmder (don't crash when `%HOME%` isn't set), and also to conditionally use the new API when available.  Plus some other minor enhancements I made.
- Port Cmder to v1.x -- will require help from Cmder and/or ConEmu teams.  There are a lot of hard-coded expectations about Clink (web site address, terminal input mode, DLL names, VirtualAlloc patterns, and many other things).
- [#12](https://github.com/chrisant996/clink/issues/12) Why is Cmder's Clink so slow to start?

## Other
- **Alt+P** then **Ctrl+G** internally resets the prompt, but `rl_redisplay()` gets confused into still drawing the cached `local_prompt`.
- If the last line of the prompt is "too long" then `rl_message()` in **Alt+P** fails to draw the adjusted prompt correctly; the old prompt continues to be drawn.
  - The cutoff is 136 characters -- less and the message shows up, or more and no message.
  - And using **Ctrl+R** and then aborting redraws the prompt using the wrong screen buffer width / wrapping position!
- Iteratively complete multiple directory levels by `b`**Tab**,**End**,**Tab**,**End**,**Tab** => after a few it completes the wrong thing!
- [#15](https://github.com/chrisant996/clink/issues/15) Quoting breaks in edge cases.
  - Probably need a callback to override `_rl_find_completion_word()`.
  - `nullcmd "abc %user`**complete** => mistakenly becomes `nullcmd "%USER` (loses the `"abc `).  Bash ignores everything inside quotes, but we want to handle env vars completions inside quotes.
  - `nullcmd "abc def"`**complete** => mistakenly becomes `nullcmd "abc def"abc def"`.  Bash mishandles this case; it becomes `nullcmd abc\ def`.

<br/>
<br/>

# RELEASE

## Issues

### Urgent

### Normal
- `match.ignore_case` can't be working correctly, and probably readline settings should determine it.  _[Is it still used by anything?]_
- `_rl_completion_case_map` isn't supported properly in clink lua APIs, nor in general.  _(The 0.4.8 implementation simply converted `-` and `_` to `?` and accepted all matches -- although maybe it filtered the results afterwards?)_
- Is it a problem that `update_internal()` gets called once per char in a key sequence?  Maybe it should only happen after a key that finishes a key binding?
- Should only fold path separators in pathish matches.
- `LOG()` certain important failure information inside Detours.
- When `convert-meta` is off, then when binding `\M-h` (etc) the key name gets interpreted differently than Clink expects.  Does this affect the `inputrc` files at all, or is it only an issue inside Clink's native code?

### Low
- Changing terminal width makes 0.4.8 slowly "walk up the screen".  Changing terminal width makes master go haywire.  Probably more ecma48 terminal issues.  Probably related to commit 8aeaa14.
- Use `path::normalise` to clean up input like "\wbin\\\\cli" when using `complete` and `menu-complete`.
- Symlink support (displaying matches, and whether to append a path separator).
- The match pipeline should not fire on pressing **Enter** after `exit`.
- [#20](https://github.com/chrisant996/clink/issues/20) Cmd gets unresponsive after "set /p" command.  _[Seems to mostly work, though `set /p FOO=""` doesn't prompt for input.]_

<br/>
<br/>

# IMPROVEMENTS

## High Priority
- Allow binding keys to Lua scripts.
  - Provide API for accessing the screen buffer, and for scrolling.
  - Eventually provide API for interacting with the Readline buffer.
- Custom color for readline input.
  - Might prefer to completely replace readline's line drawing, since it's trying to minimize updates over terminal emulators, and that makes it much harder to colorize the editing line (and arguments).

## Medium Priority
- Add a configuration setting for whether `menu-complete` wraps around.
- Support continuing completion of `"\Program Files"\`**Tab**.
  - Readline walks backward to figure out quoting.  That doesn't work reliably; must walk forward from the beginning otherwise `"\Program Files"\` is treated as though the ending `"\` is starting a new filename.
- Complete "%ENVVAR%\*" by internally expanding ENVVAR for collecting matches, but not expanding it in the editing line.

## Low Priority
- Add commands that behave like **F7** and **F8** from CMD (like `history-search-backward` without wrapping around?).
- Lua `globfiles` and `globdirs` should return whether the files and dirs are hidden, to save _N_ additional calls to look up the hidden attributes.
- [#396](https://github.com/mridgers/clink/issues/396) Pasting unicode emoji in a clink-enabled console (it works in git-bash).
- Add terminal sequences for **Ctrl+Shift+Letter** and **Ctrl+Punctuation** and etc (see https://invisible-island.net/xterm/modified-keys.html).
- Add a `history.dupe_mode` that behaves like 4Dos/4NT/Take Command from JPSoft:  **Up**/**Down** then **Enter** remembers the history position so that **Enter**, **Down**, **Enter**, **Down**, **Enter**, etc can be used to replay a series of commands.

<br/>
<br/>

# MAJOR WORK ITEMS

- **CUA Selection.**
- **Coloring arguments and flags while editing (according to Lua argmatchers).**
- **Make the match pipeline async.**
  - Spin up completion at the same moment it currently does, but make it async.
  - Only block consumers when they try to access results, if not yet complete.  Also have "try-" access that accesses if available but doesn't block (e.g. will be needed for coloring arguments while editing).
  - This will solve the UNC performance problem and also make associated pauses break-able.
  - Lua and Lua scripts will need multi-threading support.
- **Fix Readline completion coloring performance.**
  - Color output is slow because Readline writes everything one byte at a time, which incurs a huge amount of processing overhead across several layers.😭
  - Consult with Chet about how he'd like the Readline code to be structured when fixing this?

<br/>
<br/>

# INVESTIGATE

**Documentation**
- Use `npm` to run `highlight.js` at compile time?

**Popup Lists**
- What to do about completion colors?
- Make it owner draw and add text like "dir", "alias", etc?
- Add stat chars when so configured?
- Ability to delete, rearrange, and edit popup list items?
- Show the current incremental search string somewhere?

**Key Binding**
- Try to make unbound keys like **Shift-Left** tell conhost that they haven't been handled, so conhost can do its fancy CUA marking.
  - Mysteriously, holding down a bound key like **Ctrl+Up** or **Ctrl+A** was sometimes letting conhost periodically intercept some of the keypresses!  That shouldn't be possible, but maybe there's a way to deterministically cause that behavior?
  - **Ctrl+M** to activate marking mode.
  - Shouldn't be possible, but at one point **Ctrl+A** was sometimes able to get interpreted as Select All, and **Shift+Up** was sometimes able to get interpreted as Select Line Up.  That had to have been a bug in how clink was managing SetConsoleMode, but maybe there's a way to exploit that for good?
    - Maybe hook some API and call original ReadConsoleW, and feed it input somehow to trick the console host into doing its thing?

**Prompt Filtering**
- Async command prompt updating as a way to solve the delay in git repos.

**Marking**
- Marking mode in-app similar to my other shell project?  It's a kludge, but it copies with HTML formatting (and even uses the color scheme).

**Miscellaneous**
- Allow to search the console output (not command history) with a RegExp [#166](https://github.com/mridgers/clink/issues/166).  _[Unclear how that would work.  Would it scroll the console?  How would it highlight matches, etc, since that's really something the console host would need to do?  I think this needs to be implemented by the console host, e.g. conhost or ConEmu or Terminal, etc.]_

<br/>
<br/>

# MAINTENANCE

- Contact Readline owner, start conversation about possible next steps.
- Check if there's a newer update to the `wcwidth` implementation.

<br/>
<br/>

# BACKLOG

- [#542](https://github.com/mridgers/clink/issues/542) VS Code not capturing std output
- [#486](https://github.com/mridgers/clink/issues/486) **Ctrl+C** doesn't always work properly _[might be the auto-answer prompt setting]_
- A way to disable/enable clink once injected.
- A way to disable/enable prompt filtering once injected.
- [#532](https://github.com/mridgers/clink/issues/532) paste newlines, run as separate lines
  - It's pretty risky to just paste-and-go.
  - Maybe add an option to convert newlines into "&" instead?
  - Or maybe let readline do multiline editing and accept them all as a batch on **Enter**?

<br/>
<br/>

# APPENDICES

## Known Issues
- Perturbed PROMPT envvar is visible in child processes (e.g. piped shell in various file editors).
- [#531](https://github.com/mridgers/clink/issues/531) AV detects a trojan on download _[This is likely because of the use of CreateRemoteThread and/or hooking OS APIs.  There might be a way to obfuscate the fact that clink uses those, but ultimately this is kind of an inherent problem.  Getting the binaries digitally signed might be the most effective solution, but that's financially expensive.]_

## Mystery
- [#480](https://github.com/mridgers/clink/issues/480) Things don't work right when clink is in a path with spaces _[I'm not able to reproduce the problem, so dropping it off the radar for now.]_

---
Chris Antos - sparrowhawk996@gmail.com
