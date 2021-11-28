### Overview

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the well-known Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

For details, refer to the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Download

Downloads are available from the [releases](https://github.com/chrisant996/clink/releases) page.

See the [issues](https://github.com/chrisant996/clink/issues) page for known issues or to file new issues.

### Features

- The same line editing as Bash (from the [GNU Readline library](https://tiswww.case.edu/php/chet/readline/rltop.html) version 8.1).
- History persistence between sessions.
- Context sensitive completion;
  - Executables (and aliases).
  - Directory commands.
  - Environment variables.
- Context sensitive colored input text.
- Automatic suggestions.
- New keyboard shortcuts;
  - Interactive completion list (<kbd>Ctrl</kbd>+<kbd>Space</kbd>).
  - Incremental history search (<kbd>Ctrl</kbd>+<kbd>R</kbd> and <kbd>Ctrl</kbd>+<kbd>S</kbd>).
  - Powerful completion (<kbd>Tab</kbd>).
  - Undo (<kbd>Ctrl</kbd>+<kbd>Z</kbd>).
  - Environment variable expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>E</kbd>).
  - Doskey alias expansion (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd>).
  - Scroll the screen buffer (<kbd>Alt</kbd>+<kbd>Up</kbd>, etc).
  - <kbd>Shift</kbd>+Arrow keys to select text, typing replaces selected text, etc.
  - (press <kbd>Alt</kbd>+<kbd>H</kbd> for many more...)
- Directory shortcuts;
  - Typing a directory name followed by a path separator is a shortcut for `cd /d` to that directory.
  - Typing `..` or `...` is a shortcut for `cd ..` or `cd ..\..` (each additional `.` adds another `\..`).
  - Typing `-` or `cd -` changes to the previous current working directory.
- Scriptable autosuggest with Lua.
- Scriptable completion with Lua.
- Scriptable key bindings with Lua.
- Colored and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

By default Clink binds <kbd>Alt</kbd>+<kbd>H</kbd> to display the current key bindings. More features can also be found in GNU's [Readline](https://tiswww.cwru.edu/php/chet/readline/readline.html).

### Usage

There are several ways to start Clink.

1. If you installed the auto-run, just start `cmd.exe`. Run `clink autorun --help` for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `<install_dir>\clink.exe inject`.

See [Getting Started](https://chrisant996.github.io/clink/clink.html#getting-started) for information on how to get started with using Clink.

### Upgrading from Clink v0.4.9

The new Clink tries to be as backward compatible with Clink v0.4.9 as possible. However, in some cases upgrading may require a little bit of configuration work. More details can be found in the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Extending Clink

Clink can be extended through its Lua API which allows easy creation of context sensitive match generators, prompt filtering, and more. More details can be found in the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Building Clink

Clink uses [Premake](http://premake.github.io) to generate Visual Studio solutions or makefiles for MinGW. Note that Premake >= 5.0-alpha12 is required.

1. Cd to your clone of Clink.
2. Run `premake5.exe <toolchain>` (where `<toolchain>` is one of Premake's actions - see `premake5.exe --help`)
3. Build scripts will be generated in `.build\<toolchain>`. For example `.build\vs2013\clink.sln`.
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
  - Note:  If the instruction payload references any functions or variables from the Clink process, it will crash during execution inside the target CMD.exe process.

### Debugging Lua Scripts

1. Use `clink set lua.debug true` to enable using the Lua debugger.
2. Use `clink set lua.break_on_error true` to automatically break into the Lua debugger on any Lua script error.
3. Add a `pause()` line in a Lua script to break into the debugger at that spot, if the `lua.debug` setting is enabled.
4. Use `help` in the Lua debugger to get help on using the Lua debugger.

### License

Clink is distributed under the terms of the GNU General Public License v3.0.

<!-- vim: set ft=markdown : -->
