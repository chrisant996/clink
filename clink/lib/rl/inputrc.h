/* Copyright (c) 2015 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef INPUTRC_H
#define INPUTRC_H

const char* clink_inputrc[] = {
    "set bell-style               visible",
    "set completion-ignore-case   on",
    "set completion-map-case      on",
    "set completion-display-width 106",
    "set output-meta              on",    // for correct utf8 output
    "set skip-completed-text      on",
    "set visible-stats            off",

    "\"\\eOD\": backward-word",           // ctrl-left
    "\"\\eOC\": forward-word",            // ctrl-right
    "\"\\e[4\": end-of-line",             // end
    "\"\\e[1\": beginning-of-line",       // home
    "\"\\e[3\": delete-char",             // del
    "\"\\e[t\": enter-scroll-mode",       // shift-pgup
    "\"\\eO4\": kill-line",               // ctrl-end
    "\"\\eO1\": backward-kill-line",      // ctrl-home
    "\"\\e[5\": history-search-backward", // pgup
    "\"\\e[6\": history-search-forward",  // pgdn

    "set keymap emacs",
    "\"\\t\": clink-completion-shim",
    "C-v:     paste-from-clipboard",
    "C-q:     reload-lua-state",
    "C-z:     undo",
    "M-h:     show-rl-help",
    "M-C-c:   copy-line-to-clipboard",
    "C-c:     ctrl-c",
    "M-a:     \"..\\\\\"",

    "set keymap vi-insert",
    "\"\\t\": clink-completion-shim",
    "C-v:     paste-from-clipboard",
    "C-z:     undo",
    "M-h:     show-rl-help",
    "M-C-c:   copy-line-to-clipboard",
    "C-c:     ctrl-c",
    "M-a:     \"..\\\\\"",

    "set keymap vi-move",
    "C-v:     paste-from-clipboard",
    "C-z:     undo",
    "M-h:     show-rl-help",
    "M-C-c:   copy-line-to-clipboard",
    "C-c:     ctrl-c",
    "M-a:     \"..\\\\\"",

    "$if cmd.exe",
        "set keymap emacs",
        "\"\\eO5\": up-directory",
        "M-C-u:     up-directory",
        "M-C-e:     expand-env-vars",

        "set keymap vi-insert",
        "\"\\eO5\": up-directory",
        "M-C-u:     up-directory",
        "M-C-e:     expand-env-vars",

        "set keymap vi-move",
        "\"\\eO5\": up-directory",
        "M-C-u:     up-directory",
        "M-C-e:     expand-env-vars",
    "$endif",

    "set keymap emacs",
    nullptr,
};

#endif // INPUTRC_H
