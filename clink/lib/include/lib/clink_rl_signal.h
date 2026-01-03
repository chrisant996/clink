// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <core/base.h>

extern void (*after_signal_hook_fn)();

void clink_sighandler(int32 sig);
bool clink_maybe_handle_signal();

void force_signaled_redisplay();
