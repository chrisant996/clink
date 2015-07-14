// Copyright (c) 2013 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
typedef struct {
    void*   handle;
    void*   ptr;
    int     size;
} shared_mem_t;

//------------------------------------------------------------------------------
shared_mem_t*   create_shared_mem(int page_count, const char* tag, int id);
shared_mem_t*   open_shared_mem(int page_count, const char* tag, int id);
void            close_shared_mem(shared_mem_t* info);
