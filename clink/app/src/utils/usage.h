// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

extern void maybe_print_logo();
extern void puts_clink_header();
extern void puts_help(const char* const* help_pairs, const char* const* other_pairs=nullptr, int32 padding=4);
