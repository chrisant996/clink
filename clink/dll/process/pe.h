// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
void*   get_nt_headers(void* base);
void*   get_data_directory(void* base, int index, int* size);
void**  get_import_by_name(void* base, const char* dll, const char* func_name);
void**  get_import_by_addr(void* base, const char* dll, void* func_addr);
void*   get_export(void* base, const char* func_name);
