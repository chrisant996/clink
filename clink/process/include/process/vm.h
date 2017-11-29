// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class vm
{
public:
    enum
    {
        access_read     = 1 << 0,
        access_write    = 1 << 1,
        access_execute  = 1 << 2,
        access_cow      = 1 << 3, // copy-on-write
        access_rw       = access_read|access_write,
        access_rx       = access_read|access_execute,
        access_rwx      = access_read|access_execute|access_write,
    };

    struct region
    {
        void*           base;
        unsigned int    page_count;
    };

                        vm(int pid=-1);
                        ~vm();
    static size_t       get_block_granularity();
    static size_t       get_page_size();
    void*               get_alloc_base(void* address);
    region              get_region(void* address);
    void*               get_page(void* address);
    region              alloc(unsigned int page_count, unsigned int access=access_read|access_write);
    void                free(const region& region);
    int                 get_access(const region& region);
    void                set_access(const region& region, unsigned int access);
    bool                read(void* dest, const void* src, size_t size);
    bool                write(void* dest, const void* src, size_t size);
    void                flush_icache(const region& region={});

private:
    void*               m_handle;
};
