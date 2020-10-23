// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

typedef void (__stdcall *hookptr_t)();

hookptr_t hook_iat(void* base, const char* dll, const char* func_name, hookptr_t hook, int find_by_name);
hookptr_t hook_jmp(void* module, const char* func_name, hookptr_t hook);
