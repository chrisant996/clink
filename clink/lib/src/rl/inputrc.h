// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

const char* clink_inputrc[] = {
    "M-OD: backward-word",           // ctrl-left
    "M-OC: forward-word",            // ctrl-right
    "M-[4: end-of-line",             // end
    "M-[1: beginning-of-line",       // home
    "M-[3: delete-char",             // del
    "M-[t: enter-scroll-mode",       // shift-pgup
    "M-O4: kill-line",               // ctrl-end
    "M-O1: backward-kill-line",      // ctrl-home
    "M-[5: history-search-backward", // pgup
    "M-[6: history-search-forward",  // pgdn

    "set keymap emacs",
    "C-v:   paste-from-clipboard",
    "C-q:   reload-lua-state",
    "C-z:   undo",
    "M-h:   show-rl-help",
    "M-C-c: copy-line-to-clipboard",
    "C-c:   ctrl-c",
    "M-a:   \"..\\\\\"",

    "set keymap vi-insert",
    "C-v:   paste-from-clipboard",
    "C-z:   undo",
    "M-h:   show-rl-help",
    "M-C-c: copy-line-to-clipboard",
    "C-c:   ctrl-c",
    "M-a:   \"..\\\\\"",

    "set keymap vi-move",
    "C-v:   paste-from-clipboard",
    "C-z:   undo",
    "M-h:   show-rl-help",
    "M-C-c: copy-line-to-clipboard",
    "C-c:   ctrl-c",
    "M-a:   \"..\\\\\"",

#if MODE4
    "$if cmd.exe",
#endif
        "set keymap emacs",
        "M-O5:  up-directory",
        "M-C-u: up-directory",
        "M-C-e: expand-env-vars",

        "set keymap vi-insert",
        "M-O5:  up-directory",
        "M-C-u: up-directory",
        "M-C-e: expand-env-vars",

        "set keymap vi-move",
        "M-O5:  up-directory",
        "M-C-u: up-directory",
        "M-C-e: expand-env-vars",
#if MODE4
    "$endif",
#endif

    "set bell-style   visible", // MODE4
    "set convert-meta off",   // For correct utf8 input.
    "set output-meta  on",    // For correct utf8 output
    "set keymap emacs",
    nullptr,
};
