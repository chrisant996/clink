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

<p><b>Auto-Suggestions</b></p>

<p>Clink offers suggestions as you type based on history, files, and completions.</p>

<pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black"><span class="color_default">C:\dir></span><span class="color_executable">findstr</span><span class="cursor">_</span><span class="color_suggestion">/s needle haystack\*</span></span>
</code></pre>

<p>Press <kbd>Right</kbd> or <kbd>End</kbd> to accept a suggestion (shown in a muted color).</p>

</div>
<div class="promo_block">

<p><b>Completions</b></p>

<p>Clink can complete words when you press <kbd>Tab</kbd> or <kbd>Ctrl</kbd>-<kbd>Space</kbd>.</p>

<p>Built-in completions are available for executables, aliases, command names, directory commands, and environment variables.  You can use Lua scripts to add custom completions.</p>

</div>
</div>
<div class="promo_box">
<div class="promo_block">

<p><b>Persistent History</b></p>

<p>Clink stores persistent history between sessions.</p>

<ul>
<li><kbd>Up</kbd> and <kbd>Down</kbd> cycle through history entries.</li>
<li><kbd>PgUp</kbd> and <kbd>PgDn</kbd> cycle through history entries matching the typed prefix.</li>
<li><kbd>F7</kbd> show a popup list of selectable history entries.</li>
<li><kbd>Ctrl</kbd>-<kbd>R</kbd> and <kbd>Ctrl</kbd>-<kbd>S</kbd> search history incrementally.</li>
</ul>

</div>
<div class="promo_block">

<p><b>Scriptable Prompt and Colored Input</b></p>

<p>You can customize the prompt dynamically with Lua scripts -- like in other shells -- but never before possible in cmd.exe!</p>

<pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black"><span class="color_default"><span style="color:#0087ff">C:\repos\clink</span> <span style="color:#888">git</span> <span style="color:#ff0">main</span><span style="color:#888">-></span><span style="color:#ff0">origin *3</span> <span style="color:#f33">!1</span>
<span style="color:#0f0">></span> <span class="color_argmatcher">git</span> <span class="color_arg">merge</span> <span class="color_flag">--help</span><span class="cursor">_</span></span>
</code></pre>

<p>Your input is colored by context sensitive completion scripts.</p>

</div>
</div>
<div class="promo_box">
<div class="promo_block">

<p><b>Command Line Editing Improvements</b></p>

<p>Clink supercharges the command line with new input editing commands and configurable key bindings.</p>

<ul>
<li><kbd>Alt</kbd>-<kbd>H</kbd> to display all key bindings.</li>
<li><kbd>Tab</kbd> for completion.</li>
<li><kbd>Ctrl</kbd>-<kbd>Space</kbd> for an interactive completion list.</li>
<li><kbd>Ctrl</kbd>-<kbd>Z</kbd> to undo input.</li>
<li><kbd>Shift</kbd>-<kbd>Arrows</kbd> to select text, and type to replace selected text.</li>
</ul>

</div>
<div class="promo_block">

<p><b>Convenience</b></p>

<p>Optional auto-answering of the "Terminate batch job?" prompt.</p>

<p>Directory shortcuts:</p>
<ul>
<li><code>dirname\</code> is a shortcut for <code>cd /d</code> to that directory.</li>
<li><code>..</code> or <code>...</code> are shortcuts for <code>cd ..</code> or <code>cd ..\..</code> (etc).</li>
<li><code>-</code> or <code>cd -</code> changes to the previous current working directory.</li>
</ul>

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

