// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#if defined(__cplusplus) && defined(lconfig_h)
} // End the enclosing extern "C" scope!
#endif

#ifdef DEBUG
#define USE_MEMORY_TRACKING
#define INCLUDE_CALLSTACKS
#define USE_RTTI
#endif

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
void* _cdecl operator new(size_t size);
void _cdecl operator delete(void* size);

_Ret_notnull_ _Post_writable_byte_size_(size) DECLALLOCATOR
void* _cdecl operator new[](size_t size);
void _cdecl operator delete[](void* size);

#ifndef __PLACEMENT_NEW_INLINE
#   define __PLACEMENT_NEW_INLINE
inline void* _cdecl operator new(size_t, void* size) { return size; }
#   if defined(_MSC_VER)
#       if _MSC_VER >= 1200
inline void _cdecl operator delete(void*, void*) { return; }
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
    memRTTI                     = 0x00000100,
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
#ifdef USE_RTTI
#define _MEM_RTTI               , memRTTI|memNew
#endif
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
#endif

char const* dbginspectmemory(const void* pv, size_t size);

void dbgsetsanealloc(size_t maxalloc, size_t maxrealloc, size_t const* exceptions);

void* dbgalloc_(size_t size, unsigned int flags);
void* dbgrealloc_(void* pv, size_t size, unsigned int flags);
void dbgfree_(void* pv, unsigned int type);

void dbgsetlabel(const void* pv, const char* label, int copy);
void dbgverifylabel(const void* pv, const char* label);
void dbgsetignore(const void* pv, int ignore);
size_t dbgignoresince(size_t alloc_number, size_t* total_bytes, char const* label); // Caller is responsible for lifetime of label pointer.
void dbgmarkmem(const void* pv);

size_t dbggetallocnumber();
void dbgsetreference(size_t alloc_number, const char* tag);
void dbgcheck();
void dbgchecksince(size_t alloc_number);
void dbgcheckfinal();

#ifdef USE_HEAP_STATS
// TODO: report heap stats
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define dbg_snapshot_heap(var)          const size_t var = dbggetallocnumber()
#define dbg_ignore_since_snapshot(var, label)  do { dbgignoresince(var, nullptr, label); } while (false)
#endif

#else // !USE_MEMORY_TRACKING

#define _MEM_0
#define _MEM_NEW
#define _MEM_NEWARRAY
#ifdef USE_RTTI
#define _MEM_RTTI
#endif
#define _MEM_OBJECT
#define _MEM_NOSIZECHECK

#define DBG_DECLARE_MARKMEM
#define DBG_DECLARE_VIRTUAL_MARKMEM
#define DBG_DECLARE_OVERRIDE_MARKMEM

#ifdef __cplusplus
#define dbg_snapshot_heap(var)          ((void)0)
#define dbg_ignore_since_snapshot(var)  ((void)0)
#endif

#endif // !USE_MEMORY_TRACKING

//------------------------------------------------------------------------------
// Debug helpers.

#ifdef DEBUG

#ifdef __cplusplus
extern "C" {
#endif

size_t dbgcchcopy(char* to, size_t max, const char* from);
size_t dbgcchcat(char* to, size_t max, const char* from);

#ifdef __cplusplus
}
#endif

#endif // DEBUG

#if defined(__cplusplus) && defined(lconfig_h)
extern "C" { // Reopen the enclosing extern "C" scope!
#endif
