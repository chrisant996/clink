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

Clink's uses [Premake](http://premake.github.io) to generate Visual Studio solutions or makefiles for MinGW. Note that Premake >= 5.0-alpha8 is required.

1. Cd to your clone of Clink.
2. Run "premake &lt;toolchain&gt;" (where "&lt;toolchain&gt;" is one of Premake's actions - see "premake --help")
3. Build scripts will be generated in ".build\\&lt;toolchain&gt;\". For example; .build\vs2013\clink.sln.
4. Call your toolchain of choice (VS, mingw32-make.exe, msbuild.exe, etc). GNU makefiles (Premake's *gmake* target) have a **help** target for more info.

### License

Clink is distributed under the terms of the GNU General Public License v3.0.

<!-- vim: set ft=markdown : -->
