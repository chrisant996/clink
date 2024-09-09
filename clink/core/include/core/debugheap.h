// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
// Memory tracker.

#ifdef USE_MEMORY_TRACKING

#ifdef _MSC_VER
#define DECLALLOCATOR __declspec(allocator)
#define DECLRESTRICT __declspec(restrict)
#else
#define DECLALLOCATOR
#define DECLRESTRICT
#endif

#ifdef __cplusplus

_Ret_notnull_ _Post_writable_byte_size_(size) DECLALLOCATOR
void* __cdecl operator new(size_t size);
void __cdecl operator delete(void* size);

_Ret_notnull_ _Post_writable_byte_size_(size) DECLALLOCATOR
void* __cdecl operator new[](size_t size);
void __cdecl operator delete[](void* size);

#ifndef __PLACEMENT_NEW_INLINE
#   define __PLACEMENT_NEW_INLINE
inline void* __cdecl operator new(size_t, void* size) { return size; }
#   if defined(_MSC_VER)
#       if _MSC_VER >= 1200
inline void __cdecl operator delete(void*, void*) { return; }
#       endif
#   endif
#endif

#endif // __cplusplus

enum
{
#ifdef INCLUDE_CALLSTACKS
    MAX_STACK_DEPTH             = 12,
#endif

    memAllocatorMask            = 0x000000ff,   // Verify no mismatches between alloc/free allocator types.
    memImmutableMask            = 0x0000ffff,   // Verify these flags do not change.

    // Allocator types; immutable.        FF
    memNew                      = 0x00000001,
    memNewArray                 = 0x00000002,

    // Allocation flags; immutable.     FF
#ifdef USE_RTTI
    memObject                   = 0x00000100,   // Type is `object`, which has a virtual destructor and can get the type name via RTTI.
#endif
    memNoSizeCheck              = 0x00000200,
    memNoStack                  = 0x00000400,

    // Behavior flags.                FF
    memSkipOneFrame             = 0x00010000,
    memSkipAnotherFrame         = 0x00020000,
    memZeroInit                 = 0x00040000,

    // State flags.                 FF
    memIgnoreLeak               = 0x01000000,
    memMarked                   = 0x80000000,
};

#define _MEM_0                  , 0
#define _MEM_NEW                , memNew
#define _MEM_NEWARRAY           , memNewArray
#define _MEM_NOSIZECHECK        , memNoSizeCheck

#define DBG_DECLARE_MARKMEM     void markmem()
#define DBG_DECLARE_VIRTUAL_MARKMEM virtual void markmem() = 0
#define DBG_DECLARE_OVERRIDE_MARKMEM void markmem() override

#define memalloc                dbgalloc
#define memcalloc               dbgcalloc
#define memrealloc              dbgrealloc
#define memfree                 dbgfree

#ifdef __cplusplus
extern "C" {
#define _DEFAULT_ZERO           =0
#define _DEFAULT_ONE            =1
#else
#define _DEFAULT_ZERO
#define _DEFAULT_ONE
#endif

#ifndef _CSTDINT_
typedef char int8;      typedef unsigned char uint8;
typedef short int16;    typedef unsigned short uint16;
typedef int int32;      typedef unsigned int uint32;
typedef __int64 int64;  typedef unsigned __int64 uint64;
#endif

char const* dbginspectmemory(const void* pv, size_t size);

struct sane_alloc_config
{
    size_t max_sane_alloc;
    size_t max_sane_realloc;
    const size_t* sane_alloc_exceptions;    // 0 terminated list of exceptions.
};

void dbgsetsanealloc(size_t maxalloc, size_t maxrealloc, size_t const* exceptions);
void dbgsetsaneallocconfig(const struct sane_alloc_config sane);
struct sane_alloc_config dbggetsaneallocconfig();

void* dbgalloc_(size_t size, uint32 flags);
void* dbgrealloc_(void* pv, size_t size, uint32 flags);
void dbgfree_(void* pv, uint32 type);

void dbgsetlabel(const void* pv, const char* label, int32 copy);
void dbgverifylabel(const void* pv, const char* label);
void dbgsetignore(const void* pv, int32 ignore _DEFAULT_ONE);
// Caller is responsible for lifetime of label pointer.
size_t dbgignoresince(size_t alloc_number, size_t* total_bytes, char const* label _DEFAULT_ZERO, int32 all_threads _DEFAULT_ZERO);
void dbgmarkmem(const void* pv);

size_t dbggetallocnumber();
void dbgsetreference(size_t alloc_number, const char* tag _DEFAULT_ZERO);
void dbgcheck();
// When ALLOC_NUMBER is 0:
//  - all = all allocations
//  - ignored = ignored or marked allocations
//  - snapshot = leaks
// When ALLOC_NUMBER != 0 and ALL_LEAKS == 0:
//  - all = all allocations
//  - ignored = ignored or marked allocations
//  - snapshot = leaks after ALLOC_NUMBER
// When ALLOC_NUMBER != 0 and ALL_LEAKS != 0:
//  - all = all leaks
//  - ignored = ignored or marked allocations after ALLOC_NUMBER
//  - snapshot = leaks after ALLOC_NUMBER
void dbgchecksince(size_t alloc_number, int32 all_leaks _DEFAULT_ZERO);
void dbgcheckfinal();

#ifdef USE_HEAP_STATS
// TODO: report heap stats
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class dbg_ignore_scoper
{
public:
    dbg_ignore_scoper(const char* label=nullptr) : m_since(dbggetallocnumber()), m_label(label) {}
    ~dbg_ignore_scoper() { dbgignoresince(m_since, nullptr, m_label); }
private:
    size_t m_since;
    const char* m_label;
};
#define dbg_snapshot_heap(var)                  const size_t var = dbggetallocnumber()
#define dbg_ignore_since_snapshot(var, label)   do { dbgignoresince(var, nullptr, label); } while (false)
#define dbg_ignore_scope(var, label)            const dbg_ignore_scoper var(label)
#endif

#else // !USE_MEMORY_TRACKING

#define _MEM_0
#define _MEM_NEW
#define _MEM_NEWARRAY
#define _MEM_OBJECT
#define _MEM_NOSIZECHECK

#define DBG_DECLARE_MARKMEM
#define DBG_DECLARE_VIRTUAL_MARKMEM
#define DBG_DECLARE_OVERRIDE_MARKMEM

#ifdef __cplusplus
#define dbg_snapshot_heap(var)                  ((void)0)
#define dbg_ignore_since_snapshot(var, label)   ((void)0)
#define dbg_ignore_scope(var, label)            ((void)0)
#endif

#endif // !USE_MEMORY_TRACKING
