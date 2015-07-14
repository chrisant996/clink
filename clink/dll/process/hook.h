// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

void* hook_iat(void* base, const char* dll, const char* func_name, void* hook, int find_by_name);
void* hook_jmp(void* module, const char* func_name, void* hook);
