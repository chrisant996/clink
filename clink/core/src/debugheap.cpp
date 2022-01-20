// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

// WARNING:  Cannot rely on global scope constructors running before these
// functions are used.  The get_cs() and get_config() functions and their
// associated data are designed to accommodate that.

#include "pch.h"
#include "debugheap.h"

#ifdef USE_MEMORY_TRACKING

#include "callstack.h"
#include "assert_improved.h"

#include <stdlib.h>
#include <utility>
#include <assert.h>

struct mem_tracking
{
    static size_t           pad_size(size_t size);
    static mem_tracking*    from_pv(void* pv);
    void*                   to_pv();
    void                    fill(size_t size, bool dead);
    void                    link();
    void                    unlink();
    void                    make_zombie();
    void                    copy_from_and_free(mem_tracking* p, size_t size);

    char const*             get_label() const;
    void                    set_label(char const* label, bool own);

#ifdef INCLUDE_CALLSTACKS
    void                    get_stack_string(char* buffer, size_t max, bool newlines) const;
#endif

    mem_tracking*           m_prev;
    mem_tracking*           m_next;

    size_t                  m_alloc_number;
    unsigned int            m_flags;
    size_t                  m_requested_size;
    size_t                  m_count_realloc;

#ifdef INCLUDE_CALLSTACKS
    void*                   m_stack[MAX_STACK_DEPTH]; // Max or null terminated.
#endif

private:
    char const*	            m_label;            // Friendly name of allocation.
    unsigned                m_own_label : 1;    // Whether to free m_pszLabel.
};

#ifdef INCLUDE_CALLSTACKS
#include "callstack.h"
#endif

#ifdef _MSC_VER
#define DECLALLOCATOR __declspec(allocator)
#define DECLRESTRICT __declspec(restrict)
#else
#define DECLALLOCATOR
#define DECLRESTRICT
#endif

#ifdef __cplusplus
extern "C" {
#endif

DECLALLOCATOR DECLRESTRICT void* __cdecl calloc(size_t count, size_t size) { return dbgalloc_(count * size, memSkipOneFrame|memZeroInit); }
void __cdecl free(void* pv) { dbgfree_(pv, 0); }
DECLALLOCATOR DECLRESTRICT void* __cdecl malloc(size_t size) { return dbgalloc_(size, memSkipOneFrame); }
DECLALLOCATOR DECLRESTRICT void* __cdecl realloc(void* pv, size_t size) { return dbgrealloc_(pv, size, 0|memSkipOneFrame); }

#if 0
// Can't replace these, because the CRT uses them internally.
DECLALLOCATOR DECLRESTRICT void* __cdecl _calloc_base(size_t count, size_t size) { DebugBreak(); return nullptr; }
void __cdecl _free_base(void* pv) { DebugBreak(); }
DECLALLOCATOR DECLRESTRICT void* __cdecl _malloc_base(size_t size) { DebugBreak(); return nullptr; }
size_t __cdecl _msize_base(void* pv) { DebugBreak(); return 0; }
size_t __cdecl _msize(void* pv) { DebugBreak(); return 0; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _realloc_base(void* pv, size_t size) { DebugBreak(); return nullptr; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _recalloc_base(void* pv, size_t count, size_t size) { DebugBreak(); return nullptr; }
//DECLALLOCATOR DECLRESTRICT void* __cdecl _recalloc(void* pv, size_t count, size_t size) { return dbgrealloc_(pv, count * size, 0|memSkipOneFrame); }
DECLALLOCATOR DECLRESTRICT void* __cdecl _recalloc(void* pv, size_t count, size_t size) { DebugBreak(); return nullptr; }
void __cdecl _aligned_free(void* pv) { DebugBreak(); }
DECLALLOCATOR DECLRESTRICT void* __cdecl _aligned_malloc(size_t size, size_t alignment) { DebugBreak(); return nullptr; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _aligned_offset_malloc(size_t size, size_t alignment, size_t offset) { DebugBreak(); return nullptr; }
size_t __cdecl _aligned_msize(void* pv, size_t alignment, size_t offset) { DebugBreak(); return 0; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _aligned_offset_realloc(void* pv, size_t size, size_t alignment, size_t offset) { DebugBreak(); return nullptr; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _aligned_offset_recalloc(void* pv, size_t count, size_t size, size_t alignment, size_t offset) { DebugBreak(); return nullptr; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _aligned_realloc(void* pv, size_t size, size_t alignment) { DebugBreak(); return nullptr; }
DECLALLOCATOR DECLRESTRICT void* __cdecl _aligned_recalloc(void* pv, size_t count, size_t size, size_t alignment) { DebugBreak(); return nullptr; }
#endif

#if 0
// Not sure why these are failing, but will just ignore them for now.
//  libucrtd.lib(expand.obj) : error LNK2005: _expand already defined in clink_core_x64.lib(debugheap.obj)
//  libucrtd.lib(new_handler.obj) : error LNK2005: _callnewh already defined in clink_core_x64.lib(debugheap.obj)
//  bin\debug\clink_test_x64.exe : fatal error LNK1169: one or more multiply defined symbols found
int __cdecl _callnewh(size_t size) { DebugBreak(); return 0; }
DECLALLOCATOR void* __cdecl _expand(void* pv, size_t size) { DebugBreak(); return nullptr; }
#endif

#ifdef __cplusplus
} // extern "C"
#endif

static void* real_calloc(size_t count, size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size);
}

static void real_free(void* pv)
{
    HeapFree(GetProcessHeap(), 0, pv);
}

static void* real_malloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void* real_realloc(void* pv, size_t size)
{
    if (!size)
    {
        if (pv)
            real_free(pv);
        return nullptr;
    }

    if (!pv)
        return real_malloc(size);

    return HeapReAlloc(GetProcessHeap(), 0, pv, size);
}

namespace debugheap
{

#ifdef INCLUDE_CALLSTACKS
int const MAX_STACK_STRING_LENGTH = DEFAULT_CALLSTACK_LEN;
#else
int const MAX_STACK_STRING_LENGTH = 1;
#endif

struct config
{
    size_t const size_guard                 = 4;
    BYTE const allocfill                    = 0xcd;
    BYTE const reallocfill                  = 0xce;
    BYTE const deadfill                     = 0xdd;
    BYTE const memtrackdeadfill             = 0xde;
    BYTE const memtrackprerun               = 0xab; // Left chevron.
    BYTE const memtrackoverrun              = 0xbb; // Right chevron.

    volatile LONG count_entered             = 0;
    volatile DWORD thread_id                = 0;
    mem_tracking* volatile head             = nullptr;
    volatile size_t alloc_number            = 0;
    volatile size_t break_alloc_number      = 0;
    volatile size_t break_alloc_size        = 0;

    size_t max_sane_alloc                   = 65536;
    size_t max_sane_realloc                 = 1024 * 1024;
    size_t const *sane_alloc_exceptions     = nullptr; // 0 terminated list of exceptions.
};

static config& get_config()
{
    static config s_config;
    return s_config;
}

struct static_critsec : public CRITICAL_SECTION
{
public:
    static_critsec() { InitializeCriticalSection(this); }
};

#ifdef USE_HEAP_STATS
// total cumulative bytes
// total requested bytes
// snap peak requested bytes (snap is between two states)
// total peak requested bytes
// total allocations
// snap peakllocations (snap is between two states)
// total peak allocations
#endif

class auto_lock
{
public:
    auto_lock()
    {
        EnterCriticalSection(get_cs());
        auto& config = get_config();
        config.count_entered++;
        config.thread_id = GetCurrentThreadId();
    }
    ~auto_lock()
    {
        auto& config = get_config();
        config.count_entered--;
        if (!config.count_entered)
            config.thread_id = 0;
        LeaveCriticalSection(get_cs());
    }
private:
    static CRITICAL_SECTION* get_cs()
    {
        static static_critsec s_cs;
        return &s_cs;
    }
};

inline void assert_synchronized() { assert(get_config().thread_id == GetCurrentThreadId()); }

static size_t dbgnewallocnumber()
{
    assert_synchronized();

    auto& config = get_config();

    config.alloc_number++;

    if (config.break_alloc_number && config.alloc_number == config.break_alloc_number )
        DebugBreak();

    return config.alloc_number;
}

};

using namespace debugheap;

extern "C" char const* dbginspectmemory(void const* pv, size_t size)
{
    size_t const max_inspect = 32;
    static char const c_hex[] = "0123456789ABCDEF";
    static char s_hex[5 + (3 * max_inspect) + 3 + 1];
    static char s_chr[1 + (3 * max_inspect) + 1 + 3 + 1];

    BYTE const* bytes = static_cast<BYTE const*>(pv);
    char* hex = s_hex;
    char* chr = s_chr;
    char* end = nullptr;
    bool truncated = false;
    size_t count_filtered = 0;
    size_t max;

    if (!size)
        return "";

    if (size > max_inspect)
    {
        truncated = true;
        size = max_inspect;
    }
    max = size;

    hex += dbgcchcopy(hex, _countof(s_hex) - (hex - s_hex), "HEX: ");
    *(chr++) = '\"';

    while (size--)
    {
        *(hex++) = c_hex[*bytes >> 4];
        *(hex++) = c_hex[*bytes & 0x0f];
        *(hex++) = ' ';

        if (*bytes >= 32 && *bytes <= 126)
        {
            *(chr++) = *bytes;
        }
        else
        {
            *(chr++) = '.';
            count_filtered++;
        }

        // Ignore null termination of strings.
        if (!size && !*bytes && (max == 1 || *(bytes - 1)))
            end = chr - 1;

        bytes++;
    }

    if (end)
        chr = end;

    *(chr++) = '\"';
    *chr = '\0';
    *hex = '\0';

    if (truncated)
    {
        assert(strlen(hex) < _countof(s_hex) - 3);
        assert(strlen(chr) < _countof(s_chr) - 3);

        dbgcchcopy(hex, _countof(s_hex) - (hex - s_hex), "...");
        dbgcchcopy(chr, _countof(s_chr) - (chr - s_chr), "...");
    }

    return (count_filtered > (max / 2)) ? s_hex : s_chr;
}

inline BYTE* byteptr(void* pv)
{
    return static_cast<BYTE*>(pv);
}

size_t mem_tracking::pad_size(size_t size)
{
    assert(size);
    return sizeof(mem_tracking) + get_config().size_guard * 2 + size;
}

mem_tracking* mem_tracking::from_pv(void* pv)
{
    assert_synchronized();

    auto& config = get_config();

    mem_tracking* const p = pv ? reinterpret_cast<mem_tracking*>(byteptr(pv) - (sizeof(mem_tracking) + config.size_guard)) : nullptr;
    BYTE const* verify;
    size_t size;

    // Verify prerun.

    verify = byteptr(pv) - config.size_guard;
    for (size = config.size_guard; size--;)
    {
        if (verify[size] == config.memtrackprerun)
            continue;
        assert3(false,
                "Memory Prerun at 0x%p == 0x%2.2x\n\tPrerun area: %s",
                verify + size, verify[size], dbginspectmemory(verify, config.size_guard));
        break;
    }

    // Verify overrun.

    verify = byteptr(pv) + p->m_requested_size;
    for (size = 0; size < config.size_guard; size++)
    {
        if (verify[size] == config.memtrackoverrun)
            continue;
        assert3(false,
                "Memory Overrun at 0x%p == 0x%2.2x\n\tOverrun area: %s",
                verify + size, verify[size], dbginspectmemory(verify, config.size_guard));
        break;
    }

    return p;
}

void* mem_tracking::to_pv()
{
    return byteptr(this) + sizeof(mem_tracking) + get_config().size_guard;
}

void mem_tracking::fill(size_t const size, bool const dead)
{
    auto& config = get_config();
    BYTE* const bytes = byteptr(to_pv());

    if (!dead)
    {
        memset(bytes - config.size_guard, config.memtrackprerun, config.size_guard);
        memset(bytes + size, config.memtrackoverrun, config.size_guard);
    }

    memset(bytes, dead ? config.deadfill : config.allocfill, size);
}

void mem_tracking::link()
{
    assert_synchronized();

    auto& config = get_config();

#ifdef USE_HEAP_STATS
    // TODO
#endif

    m_next = config.head;
    if (config.head)
        config.head->m_prev = this;
    config.head = this;
}

void mem_tracking::unlink()
{
    assert_synchronized();

    auto& config = get_config();

#ifdef USE_HEAP_STATS
    // TODO
#endif

    if (m_next)
        m_next->m_prev = m_prev;

    if( m_prev )
        m_prev->m_next = m_next;
    else
        config.head = m_next;
}

void mem_tracking::make_zombie()
{
    if (m_own_label)
        real_free(const_cast<char*>(m_label));

    memset(this, get_config().memtrackdeadfill, sizeof(mem_tracking));
}

void mem_tracking::copy_from_and_free(mem_tracking* const p, size_t const size)
{
    assert_synchronized();

    memcpy(to_pv(), p->to_pv(), std::min(p->m_requested_size, size));

    m_alloc_number = p->m_alloc_number;
    m_flags = p->m_flags;
    m_label = p->m_label;
    m_own_label = p->m_own_label;
    m_count_realloc = p->m_count_realloc;

    p->m_own_label = false;

    dbgfree_(p->to_pv(), p->m_flags);
}

char const* mem_tracking::get_label() const
{
    if (m_label)
        return m_label;

#ifdef USE_RTTI
    if (m_flags & memRTTI)
        return TypeIdName(*to_pv());
#endif

    if (m_flags & memNewArray)
        return "(new[])";
    if (m_flags & memNew)
        return "(new)";

    return "(malloc)";
}

void mem_tracking::set_label(char const* const label, bool const own)
{
    m_label = label;
    m_own_label = label && own;
}

#ifdef INCLUDE_CALLSTACKS
void mem_tracking::get_stack_string(char* buffer, size_t max, bool newlines) const
{
    format_frames(_countof(m_stack), m_stack, buffer, max, newlines);
}
#endif

extern "C" void dbgsetsanealloc(size_t max_alloc, size_t max_realloc, const size_t *exceptions )
{
    auto& config = get_config();
    config.max_sane_alloc = max_alloc;
    config.max_sane_realloc = max_realloc;
    config.sane_alloc_exceptions = exceptions;
}

extern "C" void* dbgalloc_(size_t size, unsigned int flags)
{
#ifdef INCLUDE_CALLSTACKS
    void* stack[MAX_STACK_DEPTH];
    const int skip = 1 + !!(flags & memSkipOneFrame) + !!(flags &memSkipAnotherFrame);
    size_t const levels = (flags & memNoStack) ? 0 : get_callstack_frames(skip, _countof(stack), stack);
#endif

    auto_lock lock;

    auto& config = get_config();

    if (config.break_alloc_size && config.break_alloc_size == size)
        DebugBreak();

    if (!(flags & memNoSizeCheck) && config.max_sane_alloc)
        assert1(size <= config.max_sane_alloc, "dbgalloc:  Is this a bogus size?  (%zu bytes)", size);

    mem_tracking* const p = static_cast<mem_tracking*>(real_malloc(mem_tracking::pad_size(size)));
    if (!p)
        return p;

    // Fill values.

    memset(p, 0, sizeof(mem_tracking));
    p->fill(size, false);

    if (flags & memZeroInit)
        memset(p->to_pv(), 0, size);

    // Memory block details.

    p->m_alloc_number = dbgnewallocnumber();
    p->m_requested_size = size;
    p->m_flags = flags;

#ifdef INCLUDE_CALLSTACKS
    static_assert(sizeof(p->m_stack) == sizeof(stack), "mismatched stack array size");
    static_assert(sizeof(p->m_stack[0]) == sizeof(stack[0]), "mismatched stack frame size");
    memcpy(p->m_stack, stack, levels * sizeof(stack[0]));
    if (levels < _countof(p->m_stack))
        p->m_stack[levels] = 0;
#endif

    // Link the block into the list.

    p->link();

    // Return pointer to caller-data portion of the block.

    return p->to_pv();
}

void* dbgrealloc_(void* pv, size_t size, unsigned int flags)
{
    if (!size)
    {
        dbgfree_(pv, flags);
        return nullptr;
    }

    if (!pv)
        return dbgalloc_(size, flags | memSkipAnotherFrame);

#ifdef INCLUDE_CALLSTACKS
    void* stack[MAX_STACK_DEPTH];
    const int skip = 1 + !!(flags & memSkipOneFrame) + !!(flags &memSkipAnotherFrame);
    size_t levels = get_callstack_frames(skip, _countof(stack), stack);
    if (levels < _countof(stack))
    {
        stack[levels] = nullptr; // Null terminate.
        levels++;
    }
#endif

    auto_lock lock;
    auto& config = get_config();

    if (config.break_alloc_size && config.break_alloc_size == size)
        DebugBreak();

    mem_tracking* p;
    mem_tracking* p2 = mem_tracking::from_pv(pv);

    assert(!(p2->m_flags & (memNew|memNewArray)));
    assert2((p2->m_flags & memImmutableMask) == (flags & memImmutableMask), "dbgrealloc:  Not allowed to change flags.  (old=0x%x, new=0x%x)", p2->m_flags, flags);

    p = static_cast<mem_tracking*>(real_malloc(mem_tracking::pad_size(size)));
    if (!p)
        return p;

#ifdef ASSERT_ON_UNNECESSARY_REALLOC
    assert1(size != p2->m_requested_size, "dbgrealloc:  Why realloc when the size isn't changing?  (%zu bytes)", size);
#endif

    if (!(p2->m_flags & memNoSizeCheck) && config.max_sane_realloc)
        assert1(size <= config.max_sane_realloc, "dbgrealloc:  Is this a bogus size?  (%zu bytes)", size);

    // Fill values.

    memset(p, 0, sizeof(mem_tracking));
    p->fill(size, false);

    // Copy from pv.

    p->copy_from_and_free(p2, size);

    // Update memory block details.

    p->m_count_realloc++;
    p->m_requested_size = size;

#ifdef INCLUDE_CALLSTACKS
    static_assert(sizeof(p->m_stack) == sizeof(stack), "mismatched stack array size");
    static_assert(sizeof(p->m_stack[0]) == sizeof(stack[0]), "mismatched stack frame size");
    memcpy(p->m_stack, stack, levels * sizeof(stack[0]));
#endif

    // Link the block into the list.

    p->link();

    // Return pointer to caller-data portion of the block.

    return p->to_pv();
}

void dbgfree_(void* pv, unsigned int type)
{
    if (!pv)
        return;

    auto_lock lock;
    auto& config = get_config();

    mem_tracking* p = mem_tracking::from_pv(pv);

    // TODO: Detect freeing pointer that was not allocated.

    // Match allocator type.

    if ((type & memAllocatorMask) != (p->m_flags & memAllocatorMask))
    {
        const char* free_name = "free";
        if (type & memNewArray) free_name = "delete[]";
        if (type & memNew) free_name = "delete";

        const char* alloc_name = "malloc";
        if (p->m_flags & memNewArray) alloc_name = "new[]";
        if (p->m_flags & memNew) alloc_name = "new";

        assert2(false, "ERROR:  Incorrect allocator!\n\nAllocated with %s.\nFreed with %s.", alloc_name, free_name);
    }

    // Dead fill.

    p->fill(p->m_requested_size, true);

    // Unlink the memory block from the list.

    p->unlink();

    // More dead fill.

    memset(p, config.memtrackdeadfill, sizeof(mem_tracking));

    // Free the memory.

    real_free(p);
}

static size_t list_leaks(size_t alloc_number, bool report)
{
    auto_lock lock;
    auto& config = get_config();

    mem_tracking* p;
    size_t leaks = 0;
    size_t size = 0;
    char stack[MAX_STACK_STRING_LENGTH];

    const bool newlines = false;

    if (report)
    {
        if (alloc_number)
            dbgtracef("----- Checking for leaks since #%zd -----", alloc_number);
        else
            dbgtracef("----- Checking for leaks -----");
    }

    for (p = config.head; p; p = p->m_next)
    {
        if (p->m_alloc_number <= alloc_number)
            continue;

        ++leaks;
        size += p->m_requested_size;

        if (report)
        {
            stack[0] = '\0';
#ifdef INCLUDE_CALLSTACKS
            if (p->m_stack[0])
                p->get_stack_string(stack, _countof(stack), newlines);
#endif
            dbgtracef("Leak:  #%zd,  %zu bytes (%zu reallocs),  0x%p,  %s,  %s%s%s",
                    p->m_alloc_number, p->m_requested_size, p->m_count_realloc,
                    p->to_pv(), p->get_label(), dbginspectmemory(p->to_pv(), p->m_requested_size),
                    (!stack[0] ? "" : newlines ? "\r\n" : ",  context:"), stack);
        }
    }

    if (report)
    {
        dbgtracef("----- %zd leaks, %zd bytes total -----", leaks, size);
#ifdef USE_HEAP_STATS
    // TODO
#endif
    }

    return leaks;
}

extern "C" void dbgsetlabel(void* pv, char const* label, bool copy)
{
    mem_tracking* const p = mem_tracking::from_pv(pv);

    if (copy)
    {
        size_t const max = strlen(label) + sizeof(*label);
        char* const copied_label = static_cast<char*>(real_malloc(max));
        dbgcchcopy(copied_label, max, label);
        label = copied_label;
    }

    p->set_label(label, copy);
}

extern "C" void dbgdeadfillpointer(void** ppv)
{
    memset(ppv, get_config().deadfill, sizeof(*ppv));
}

extern "C" size_t dbggetallocnumber()
{
    auto_lock lock;
    return get_config().alloc_number;
}

extern "C" void dbgcheck()
{
    auto_lock lock;

    size_t const leaks = list_leaks(0, false);

    if (leaks)
    {
        assert1(false, "%zd leaks detected.", leaks);
        list_leaks(0, true);
    }
}

extern "C" void dbgchecksince(size_t alloc_number)
{
    auto_lock lock;

    size_t const leaks = list_leaks(alloc_number, false);

    if (leaks)
    {
        assert1(false, "%zd leaks detected.", leaks);
        list_leaks(alloc_number, true);
    }
}

extern "C" void dbgcheckfinal()
{
    auto_lock lock;

    size_t const leaks = list_leaks(0, true);

    if (leaks)
    {
        assert1(false, "%zd leaks detected.", leaks);
        list_leaks(0, true);
    }
}

#ifdef USE_HEAP_STATS
// TODO: report heap stats
#endif // USE_HEAP_STATS

#endif // USE_MEMORY_TRACKING

#ifdef DEBUG

extern "C" size_t dbgcchcopy(char* to, size_t max, char const* from)
{
    if (!max)
        return 0;

    size_t copied = 0;
    while (--max && *from)
    {
        *(to++) = *(from++);
        copied++;
    }
    *to = '\0';
    return copied;
}

extern "C" size_t dbgcchcat(char* to, size_t max, char const* from)
{
    if (!max)
        return 0;

    max--;

    size_t len = strlen(to);
    if (len > max)
        len = max;
    to += len;

    while (len < max && *from)
    {
        *(to++) = *(from++);
        len++;
    }
    *to = '\0';
    return len;
}

#endif

_Ret_notnull_ _Post_writable_byte_size_(size)
void* _cdecl operator new(size_t size)
{
    return dbgalloc_(size _MEM_NEW|memSkipOneFrame);
}

void _cdecl operator delete(void* pv)
{
    if (pv)
        dbgfree_(pv _MEM_NEW);
}

_Ret_notnull_ _Post_writable_byte_size_(size)
void* _cdecl operator new[](size_t size)
{
    return dbgalloc_(size _MEM_NEWARRAY|memSkipOneFrame);
}

void _cdecl operator delete[](void* pv)
{
    if (pv)
        dbgfree_(pv _MEM_NEWARRAY);
}
