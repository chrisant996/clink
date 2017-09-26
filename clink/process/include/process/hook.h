// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

typedef void (__stdcall *funcptr_t)();

funcptr_t hook_iat(void* base, const char* dll, const char* func_name, funcptr_t hook, int find_by_name);
funcptr_t hook_jmp(void* module, const char* func_name, funcptr_t hook);
