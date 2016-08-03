// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

const char* clink_inputrc[] = {
    "set keymap emacs",
    "C-v:   paste-from-clipboard",
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

    "set keymap emacs",
    nullptr,
};
