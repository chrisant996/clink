### Overview

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the Unix shell Bash, the standard shell for many Linux distributions.

For details, refer to the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

> [!NOTE]
> Starting Clink injects it into a `cmd.exe` process, where it intercepts a handful of Windows API functions so that it can replace the prompt and input line editing with its own Readline-powered enhancements.

### Download

Downloads are available from the [releases page](https://github.com/chrisant996/clink/releases).

See the [issues page](https://github.com/chrisant996/clink/issues) for known issues or to file new issues.

### Feature Highlights

- [Auto-Suggestions](https://chrisant996.github.io/clink/clink.html#gettingstarted_autosuggest); Clink offers suggestions as you type based on history, files, and completions.
- [Completions](https://chrisant996.github.io/clink/clink.html#how-completion-works); Clink can complete words when you press <kbd>Tab</kbd> or <kbd>Ctrl</kbd>-<kbd>Space</kbd> (interactive completion list).
- [Persistent History](https://chrisant996.github.io/clink/clink.html#saved-command-history); Clink stores persistent history between sessions.
- [Scriptable Prompt](https://chrisant996.github.io/clink/clink.html#customizing-the-prompt); You can customize the prompt dynamically with Lua scripts -- like in other shells -- but never before possible in cmd.exe!
- [Colored Input Line](https://chrisant996.github.io/clink/clink.html#gettingstarted_colors); Your input is colored by context sensitive completion scripts.
- [Command Line Editing Improvements](https://chrisant996.github.io/clink/clink.html#gettingstarted_keybindings); Clink supercharges the command line with new input editing commands and configurable key bindings.
- Auto-answering of the "Terminate batch job?" prompt.
- and much more!

### Installation

You can install Clink by running the setup EXE file from the [releases page](https://github.com/chrisant996/clink/releases).

Or by using [winget](https://learn.microsoft.com/en-us/windows/package-manager/winget/) and running `winget install clink`.

Or by using [scoop](https://scoop.sh/) and running `scoop install clink`.

Or by downloading the ZIP file from [releases page](https://github.com/chrisant996/clink/releases), and extracting the files to a directory of your choosing.

### Usage

Once installed, there are several ways to start Clink.

1. If Clink is configured for autorun, just start `cmd.exe` and Clink is automatically injected and ready to use.

    > The setup EXE has an option "Autorun when cmd.exe starts".  If you didn't use the setup EXE, or if you want to enable or disable autorun later, you can run `clink autorun install` or `clink autorun uninstall` to change the autorun configuration.  Run `clink autorun --help` for more info.

2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `clink inject`.

    > If the Clink install directory isn't in the PATH, then use <code><span class="arg">install_dir</span>\clink</code> in place of `clink` to run Clink commands.  Once Clink is injected into a `cmd.exe` process, then it automatically sets an alias so that you can simply use `clink`.

You can use Clink right away without configuring anything:

- Searchable [command history](#saved-command-history) will be saved between sessions.
- <kbd>Tab</kbd> and <kbd>Ctrl</kbd>+<kbd>Space</kbd> will do match completion two different ways.
- Press <kbd>Alt</kbd>+<kbd>H</kbd> to see a list of the current key bindings.
- Press <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>/</kbd> followed by another key to see what command is bound to the key.

See [Getting Started](https://chrisant996.github.io/clink/clink.html#getting-started) for information on how to get started with using Clink.

### Upgrading from Clink v0.4.9

The new Clink tries to be as backward compatible with Clink v0.4.9 as possible. However, in some cases upgrading may require a little bit of configuration work. More details can be found in the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Extending Clink

Clink can be extended through its Lua API which allows easy creation of context sensitive match generators, prompt filtering, and more. More details can be found in the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Building Clink

Clink uses [Premake](http://premake.github.io) to generate Visual Studio solutions or makefiles for MinGW. Note that Premake >= 5.0-alpha12 is required.

1. Cd to your clone of Clink.
2. Run <code>premake5.exe <span class="arg">toolchain</span></code> (where <span class="arg">toolchain</span> is one of Premake's actions - see `premake5.exe --help`)
3. Build scripts will be generated in <code>.build\<span class="arg">toolchain</span></code>. For example `.build\vs2019\clink.sln`.
4. Call your toolchain of choice (VS, mingw32-make.exe, msbuild.exe, etc). GNU makefiles (Premake's *gmake* target) have a **help** target for more info.

### Building Documentation

1. Run `npm install -g marked` to install the [marked](https://marked.js.org) markdown library.
2. Run `premake5.exe docs`.

### Debugging Clink

1. Start Clink using any of the normal ways.
2. Launch a debugger such as Visual Studio.
3. Attach the debugger to the CMD.exe process that Clink was injected into.
   - If you break into the debugger now, it will be inside Clink code, waiting for keyboard input.
4. Here are some breakpoints that might be useful:
   - `host::edit_line` is the start of showing a prompt and accepting input.
   - `line_editor_impl::update_matches` is where the match pipeline uses `.generate()` to collect matches and `.select()` to filter the matches.
   - `rl_module::on_input` and `readline_internal_char` (and the `_rl_dispatch` inside it) is where keys are translated through Readline's keymap to invoke commands.
   - `rl_complete` or `rl_menu_complete` or `rl_old_menu_complete` are the Readline completion commands.
   - `alternative_matches` builds a Readline match array from the results collected by the match pipeline.

#### Debugging Clink startup

The easiest way to debug Clink startup is to use simulated injection rather than real injection:  set a breakpoint on `initialise_clink` and start `clink testbed --hook` under the debugger.  All of the usual Clink startup code is executed, but the cross-process injection is only simulated, so the resulting prompts of course are not actually executed by CMD.exe.

To debug Clink startup during real injection, you must attach the debugger to the target CMD.exe _before_ injection, set a breakpoint on `initialise_clink` (it will be an unresolved breakpoint at first), and then use `clink inject -p process_id`.  The debugger should resolve the breakpoint as soon as the Clink DLL is loaded, and should hit the breakpoint soon after.

#### Debugging Clink DLL injection

To debug the actual DLL injection procedure, you must debug both the `clink_x64.exe` (or `clink_x86.exe`) process and the target CMD.exe process.
- Set a breakpoint on `process::remote_call_internal` in the Clink process.
  - The first time it's reached should be for injecting a `LoadLibrary` call into the target CMD.exe process.
  - The second time it's reached should be for injecting an `initialise_clink` call into the target CMD.exe process.
- Set a breakpoint on `initialise_clink` in the target CMD.exe process.
- Step through the `remote_call_internal` function to inspect the local side of the operation.
  - The `stdcall_thunk` function is the instruction payload that will be copied into the target CMD.exe process.
  - Observe the value of `region.base` before the `CreateRemoteThread` call executes -- this is the address of the instruction payload that has been copied into the target CMD.exe process.
  - Set a breakpoint in the target CMD.exe process for that address -- when `CreateRemoteThread` executes, it will transfer execution to that address in the target CMD.exe process, and you can debug through the execution of the instruction payload itself.

> **Note:**  If the instruction payload references any functions or variables from the Clink process, it will crash during execution inside the target CMD.exe process.  Compiler features like the "just my code", "edit and continue", "omit frame pointers", exception handling, inlining, and runtime checks must be configured appropriately to keep the instruction payload self-contained (see the "clink_process" lib in the premake5.lua file).

### Debugging Lua Scripts

1. Use `clink set lua.debug true` to enable using the Lua debugger.
2. Use `clink set lua.break_on_error true` to automatically break into the Lua debugger on any Lua script error.
3. Add a `pause()` line in a Lua script to break into the debugger at that spot, if the `lua.debug` setting is enabled.
4. Use `help` in the Lua debugger to get help on using the Lua debugger.

### Ingesting new Readline versions

Perform a 3-way merge over the Readline sources where:
- Base = Readline sources from previous Readline version
- Theirs = Readline sources from updated Readline version
- Yours = Clink sources for Readline

Watch out for changes that may need additional follow-up!
Including but not limited to these, for example:
- New config variables in bind.c need to be added to `save_restore_initial_states` in rl_module.cpp.
- Some config variables may be incompatible with Clink and may need compensating changes.
- Use of `'/'` literals instead of calling `rl_is_path_separator()` (or in complete.c calling `pathfold()`).
- Changes in the `COLOR_SUPPORT` code.
- Changes in `HANDLE_MULTIBYTE` support, which may be incorrect or incomplete on Windows.
- Changes in keyboard timeout support, an area of Readline that requires shimming and workarounds in order to even compile for Windows, and doesn't behave the same as Readline expects.
- Changes in signal handler usage, particularly for `SIGALRM` or `SIGTERM` or most other signaler events since MSVC only supports `SIGINT` and `SIGBREAK` (which is a Microsoft extension).

### License

Clink is distributed under the terms of the GNU General Public License v3.0.

### Star History

<a href="https://star-history.com/#chrisant996/clink&mridgers/clink&Date">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=chrisant996/clink%2Cmridgers/clink&type=Date&theme=dark" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=chrisant996/clink%2Cmridgers/clink&type=Date" />
    <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=chrisant996/clink%2Cmridgers/clink&type=Date" />
  </picture>
</a>

<!-- vim: set ft=markdown : -->
