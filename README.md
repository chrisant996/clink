<style>
.promo_box {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    gap: 1rem 2rem;
    width: 100%;
}
.promo_block {
    flex:1 1 480px;
    background-color: #f8f8f8;
    border: 1px solid #ddd;
    border-radius: 0.2rem;
    padding-left: 1rem;
    padding-right: 1rem;
}

.color_default { color: #c0c0c0 }

.color_arg { color: #ffffff }
.color_arginfo { color: #d78700 }
.color_argmatcher { color: #00d700 }
.color_cmd { color: #ffffff }
.color_cmdredir { color: #d78700 }
.color_cmdsep { color: #af5fff }
.color_description { color: #00afff }
.color_doskey { color: #5fafff }
.color_executable { color: #0087ff }
.color_flag { color: #87d7ff }
.color_input { color: #ffaf00 }
.color_suggestion { color: #808080 }
.color_unexpected { color: #c0c0c0 }
.color_unrecognized { color: #ff5f5f }

.cursor {
    animation: blinker 1s steps(1, end) infinite;
    color: #ffffff;
}
@keyframes blinker {
    0% { opacity: 1; }
    50% { opacity: 0; }
}

code {
    vertical-align: text-top;
    display: inline-block;
    font-family: "Fira Mono",monospace;
    font-size: 0.9rem;
    padding: 0px 0.2rem;
    margin: auto 0px;
    background-color: #eee;
    border: 1px solid #ddd;
    border-radius: 0.2rem;
}

pre {
    margin: 0.15rem 0.5rem;
    padding: 0;
}

pre code {
    border: none;
    padding: 8px;
    border-radius: inherit;
}
</style>

### Overview

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the Unix shell Bash, the standard shell for Mac OS X and many Linux distributions.

For details, refer to the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Download

Downloads are available from the [releases](https://github.com/chrisant996/clink/releases) page.

See the [issues](https://github.com/chrisant996/clink/issues) page for known issues or to file new issues.

### Feature Highlights

<div class="promo_box">
<div class="promo_box">
<div class="promo_block">

**Auto-Suggestions**

Clink offers suggestions as you type based on history, files, and completions.

<pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black"><span class="color_default">C:\dir></span><span class="color_executable">findstr</span><span class="cursor">_</span><span class="color_suggestion">/s needle haystack\*</span></span>
</code></pre>

Press <kbd>Right</kbd> or <kbd>End</kbd> to accept a suggestion (shown in a muted color).

</div>
<div class="promo_block">

**Completions**

Clink can complete words when you press <kbd>Tab</kbd> or <kbd>Ctrl</kbd>-<kbd>Space</kbd>.

Built-in completions are available for executables, aliases, command names, directory commands, and environment variables.  You can use Lua scripts to add custom completions.

</div>
</div>
<div class="promo_box">
<div class="promo_block">

**Persistent History**

Clink stores persistent history between sessions.

- <kbd>Up</kbd> and <kbd>Down</kbd> cycle through history entries.
- <kbd>PgUp</kbd> and <kbd>PgDn</kbd> cycle through history entries matching the typed prefix.
- <kbd>F7</kbd> show a popup list of selectable history entries.
- <kbd>Ctrl</kbd>-<kbd>R</kbd> and <kbd>Ctrl</kbd>-<kbd>S</kbd> search history incrementally.

</div>
<div class="promo_block">

**Scriptable Prompt and Colored Input**

You can customize the prompt dynamically with Lua scripts -- like in other shells -- but never before possible in cmd.exe!

<pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black"><span class="color_default"><span style="color:#0087ff">C:\repos\clink</span> <span style="color:#888">git</span> <span style="color:#ff0">main</span><span style="color:#888">-></span><span style="color:#ff0">origin *3</span> <span style="color:#f33">!1</span>
<span style="color:#0f0">></span> <span class="color_argmatcher">git</span> <span class="color_arg">merge</span> <span class="color_flag">--help</span><span class="cursor">_</span></span>
</code></pre>

Your input is colored by context sensitive completion scripts.

</div>
</div>
<div class="promo_box">
<div class="promo_block">

**Command Line Editing Improvements**

Clink supercharges the command line with new input editing commands and configurable key bindings.

- <kbd>Alt</kbd>-<kbd>H</kbd> to display all key bindings.
- <kbd>Tab</kbd> for completion.
- <kbd>Ctrl</kbd>-<kbd>Space</kbd> for an interactive completion list.
- <kbd>Ctrl</kbd>-<kbd>Z</kbd> to undo input.
- <kbd>Shift</kbd>-<kbd>Arrows</kbd> to select text, and type to replace selected text.

</div>
<div class="promo_block">

**Convenience**

Optional auto-answering of the "Terminate batch job?" prompt.

Directory shortcuts:
- `dirname\` is a shortcut for `cd /d` to that directory.
- `..` or `...` are shortcuts for `cd ..` or `cd ..\..` (etc).
- `-` or `cd -` changes to the previous current working directory.

</div>
</div>
</div>

### Usage

There are several ways to start Clink.

1. If you installed the auto-run, just start `cmd.exe`.  Run `clink autorun --help` for more info.
2. To manually start, run the Clink shortcut from the Start menu (or the clink.bat located in the install directory).
3. To establish Clink to an existing `cmd.exe` process, use `<install_dir>\clink.exe inject`.

You can use Clink right away without configuring anything:

- Searchable [command history](#saved-command-history) will be saved between sessions.
- <kbd>Tab</kbd> and <kbd>Ctrl</kbd>+<kbd>Space</kbd> will do match completion two different ways.
- Press <kbd>Alt</kbd>+<kbd>H</kbd> to see a list of the current key bindings.
- Press <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>/</kbd> followed by another key to see what command is bound to the key.

See [Getting Started](https://chrisant996.github.io/clink/clink.html#getting-started) for information on getting started with Clink.

### Upgrading from Clink v0.4.9

The new Clink tries to be as backward compatible with Clink v0.4.9 as possible. However, in some cases upgrading may require a little bit of configuration work. More details can be found in the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

### Extending Clink

Clink can be extended through its Lua API which allows easy creation of context sensitive match generators, prompt filtering, and more. More details can be found in the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

