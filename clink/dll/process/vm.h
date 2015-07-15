// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class vm_access
{
public:
                vm_access(int pid=-1);
                ~vm_access();
    void*       alloc(size_t size);
    bool        free(void* address);
    bool        write(void* dest, const void* src, size_t size);
    bool        read(void* dest, const void* src, size_t size);

private:
    HANDLE      m_handle;
};



//------------------------------------------------------------------------------
class vm_region
{
public:
    enum {
        readable    = 1 << 0,
        writeable   = 1 << 1,
        executable  = 1 << 2,
        copyonwrite = 1 << 3,
    };

                vm_region(void* address);
                ~vm_region();
    vm_region   get_parent() const;
    void*       get_base() const;
    size_t      get_size() const;
    int         get_access() const;
    void        set_access(int flags, bool permanent=false);
    void        add_access(int flags, bool permanent=false);
    void        remove_access(int flags, bool permanent=false);

protected:
    void*       m_parent_base;
    void*       m_base;
    size_t      m_size;
    int         m_access;
    bool        m_modified;
};

//------------------------------------------------------------------------------
inline vm_region vm_region::get_parent() const
{
    return vm_region(m_parent_base);
}

//------------------------------------------------------------------------------
inline void* vm_region::get_base() const
{
    return m_base;
}

//------------------------------------------------------------------------------
inline size_t vm_region::get_size() const
{
    return m_size;
}

//------------------------------------------------------------------------------
inline int vm_region::get_access() const
{
    return m_access;
}

//------------------------------------------------------------------------------
inline void vm_region::add_access(int flags, bool permanent)
{
    set_access(m_access | flags, permanent);
}

//------------------------------------------------------------------------------
inline void vm_region::remove_access(int flags, bool permanent)
{
    set_access(m_access & ~flags, permanent);
}
