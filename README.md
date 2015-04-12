### Overview

Clink combines the native Windows shell **cmd.exe** with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the well-known Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

### Download

Downloads for the latest release of Clink can be found [here](https://github.com/mridgers/clink/releases).

### Features

- The same line editing as Bash (from GNU's Readline library).
- History persistence between sessions.
- Context sensitive completion;
    - Executables (and aliases).
    - Directory commands.
    - Environment variables
    - Thirdparty tools; Git, Mercurial, SVN, Go, and P4.
- New keyboard shortcuts;
    - Paste from clipboard (**Ctrl-V**).
    - Incremental history search (**Ctrl-R/Ctrl-S**).
    - Powerful completion (**TAB**).
    - Undo (**Ctrl-Z**).
    - Automatic "cd .." (**Ctrl-PgUp**).
    - Environment variable expansion (**Ctrl-Alt-E**).
    - (press **Alt-H** for many more...)
- Scriptable completion with Lua.
- Coloured and scriptable prompt.
- Auto-answering of the "Terminate batch job?" prompt.

### Usage

There are a variety of ways to start Clink;

1. If you installed the auto-run, just start **cmd.exe**. Run **clink autorun --help** for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing cmd.exe process, use "&lt;install_dir&gt;\clink.exe inject"

### Extending Clink

Clink can be extended through its Lua API which allows easy creation context sensitive match generators, prompt filtering, and more. More details can be found in Clink's documentation which can be found [here](https://github.com/mridgers/clink/blob/master/docs/clink.md).

### Building Clink

Clink's uses [Premake](http://premake.github.io) to generate Visual Studio solutions or makefiles for MinGW. Note that Premake >= 5.0 is required.

1. Cd to your clone of Clink.
2. Run "premake &lt;toolchain&gt;" (where "&lt;toolchain&gt;" is one of Premake's actions - see "premake --help")
3. Build scripts will be generated in ".build\\&lt;toolchain&gt;\". For example; .build\vs2013\clink.sln.
4. Call your toolchain of choice (VS, mingw32-make.exe, msbuild.exe, etc). GNU makefiles (Premake's *gmake* target) have a **help** target for more info.

### Development Builds

Periodic builds from the Git repository can be found [here](https://www.dropbox.com/sh/hqbrpkf0dpmmizq/gGX4XWAWIA).

### License

Clink is distributed under the terms of the GNU General Public License v3.0.

### Hotkeys

The list below can also be displayed within Clink by pressing Alt-h. More information on the Readline functions can be found in the Readline [manual](http://tinyurl.com/oum26rp).

| Hotkey          | Readline function                      |
|-----------------|----------------------------------------|
| `Ctrl-@`        | set-mark                               |
| `Ctrl-a`        | beginning-of-line                      |
| `Ctrl-b`        | backward-char                          |
| `Ctrl-c`        | ctrl-c                                 |
| `Ctrl-d`        | delete-char                            |
| `Ctrl-e`        | end-of-line                            |
| `Ctrl-f`        | forward-char                           |
| `Ctrl-g`        | abort                                  |
| `Ctrl-h`        | backward-delete-char                   |
| `Ctrl-i`        | clink-completion-shim                  |
| `Ctrl-j`        | accept-line                            |
| `Ctrl-k`        | kill-line                              |
| `Ctrl-l`        | clear-screen                           |
| `Ctrl-m`        | accept-line                            |
| `Ctrl-n`        | next-history                           |
| `Ctrl-p`        | previous-history                       |
| `Ctrl-q`        | reload-lua-state                       |
| `Ctrl-r`        | reverse-search-history                 |
| `Ctrl-s`        | forward-search-history                 |
| `Ctrl-t`        | transpose-chars                        |
| `Ctrl-u`        | unix-line-discard                      |
| `Ctrl-v`        | paste-from-clipboard                   |
| `Ctrl-w`        | unix-word-rubout                       |
| `Ctrl-y`        | yank                                   |
| `Ctrl-z`        | undo                                   |
| `Ctrl-]`        | character-search                       |
| `Ctrl-_`        | undo                                   |
| `Ctrl-Alt-c`    | copy-line-to-clipboard                 |
| `Ctrl-Alt-e`    | expand-env-vars                        |
| `Ctrl-Alt-g`    | abort                                  |
| `Ctrl-Alt-h`    | backward-kill-word                     |
| `Ctrl-Alt-i`    | tab-insert                             |
| `Ctrl-Alt-j`    | vi-editing-mode                        |
| `Ctrl-Alt-m`    | vi-editing-mode                        |
| `Ctrl-Alt-r`    | revert-line                            |
| `Ctrl-Alt-u`    | up-directory                           |
| `Ctrl-Alt-y`    | yank-nth-arg                           |
| `Ctrl-Alt-[`    | complete                               |
| `Ctrl-Alt-]`    | character-search-backward              |
| `Alt-`          | set-mark                               |
| `Alt-#`         | insert-comment                         |
| `Alt-&`         | tilde-expand                           |
| `Alt-*`         | insert-completions                     |
| `Alt--`         | digit-argument                         |
| `Alt-.`         | yank-last-arg                          |
| `Alt-[0-9]`     | digit-argument                         |
| `Alt-<`         | beginning-of-history                   |
| `Alt-=`         | possible-completions                   |
| `Alt->`         | end-of-history                         |
| `Alt-?`         | possible-completions                   |
| `Alt-\`         | delete-horizontal-space                |
| `Alt-_`         | yank-last-arg                          |
| `Alt-b`         | backward-word                          |
| `Alt-c`         | capitalize-word                        |
| `Alt-d`         | kill-word                              |
| `Alt-f`         | forward-word                           |
| `Alt-h`         | show-rl-help                           |
| `Alt-l`         | downcase-word                          |
| `Alt-n`         | non-incremental-forward-search-history |
| `Alt-p`         | non-incremental-reverse-search-history |
| `Alt-r`         | revert-line                            |
| `Alt-t`         | transpose-words                        |
| `Alt-u`         | upcase-word                            |
| `Alt-y`         | yank-pop                               |
| `Alt-~`         | tilde-expand                           |
| `Ctrl-x,Ctrl-g` | abort                                  |
| `Ctrl-x,Ctrl-r` | re-read-init-file                      |
| `Ctrl-x,Ctrl-u` | undo                                   |
| `Ctrl-x,Ctrl-x` | exchange-point-and-mark                |
| `Ctrl-x,(`      | start-kbd-macro                        |
| `Ctrl-x,)`      | end-kbd-macro                          |
| `Ctrl-x,e`      | call-last-kbd-macro                    |

<!-- vim: ft=markdown
-->
