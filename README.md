### Overview

Clink combines the native Windows shell cmd.exe with the powerful command line editing features of the GNU Readline library, which provides rich completion, history, and line-editing capabilities. Readline is best known for its use in the Unix shell Bash, the standard shell for many Linux distributions.

For details, refer to the [Clink documentation](https://chrisant996.github.io/clink/clink.html).

> [!NOTE]
> Starting Clink injects it into a `cmd.exe` process, where it intercepts a handful of Windows API functions so that it can replace the prompt and input line editing with its own Readline-powered enhancements.

### Download

Downloads are available from the [releases page](https://github.com/chrisant996/clink/releases).

See the [issues page](https://github.com/chrisant996/clink/issues) for known issues or to file new issues.

### Feature Highlights

<div class="promo_box">
<div class="promo_box">
<div class="promo_block">

<p><b>Auto-Suggestions</b></p>

<p>Clink offers suggestions as you type based on history, files, and completions.</p>

<pre style="border-radius:initial;border:initial;background-color:black"><code class="plaintext" style="background-color:black"><span class="color_default">C:\dir><span class="color_executable">findstr</span><span class="cursor">_</span><span class="color_suggestion">/s needle haystack\*</span></span>
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

