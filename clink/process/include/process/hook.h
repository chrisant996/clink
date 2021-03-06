// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

typedef void (__stdcall *hookptr_t)();

hookptr_t hook_iat(void* base, const char* dll, const char* func_name, hookptr_t hook, int find_by_name);

struct repair_iat_node;

bool add_repair_iat_node(repair_iat_node*&list, void* base, const char* dll, const char* func_name, hookptr_t trampoline, bool find_by_name=true);
void apply_repair_iat_list(repair_iat_node*& list);
void free_repair_iat_list(repair_iat_node*& list);
