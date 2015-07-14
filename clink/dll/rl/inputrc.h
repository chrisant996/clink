// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

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

#if MODE4
    "$if cmd.exe",
#endif
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
#if MODE4
    "$endif",
#endif

    "set keymap emacs",
    nullptr,
};
