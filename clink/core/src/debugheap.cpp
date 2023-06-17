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
#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <utility>
#include <assert.h>

#ifdef USE_RTTI
#include <typeinfo>
#endif

struct mem_tracking
{
    static size_t           pad_size(size_t size);
    static mem_tracking*    from_pv(const void* pv);
    void*                   to_pv();
    void                    fill(size_t size, bool dead);
    void                    link();
    void                    unlink();
    void                    make_zombie();
    void                    copy_from_and_free(mem_tracking* p, size_t size);

    char const*             get_label();
    void                    set_label(char const* label, bool own);

#ifdef INCLUDE_CALLSTACKS
    void                    get_stack_string(char* buffer, size_t max, bool newlines) const;
#endif

    mem_tracking*           m_prev;
    mem_tracking*           m_next;

    size_t                  m_alloc_number;
    size_t                  m_requested_size;
    uint32                  m_count_realloc;
    uint32                  m_flags;
    DWORD                   m_thread;

#ifdef INCLUDE_CALLSTACKS
    DWORD                   m_hash;
    void*                   m_stack[MAX_STACK_DEPTH]; // Max or null terminated.
#endif

private:
    char const*	            m_label;            // Friendly name of allocation.
    unsigned                m_own_label : 1;    // Whether to free m_pszLabel.
};

#ifdef INCLUDE_CALLSTACKS
#include "callstack.h"
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
int32 __cdecl _callnewh(size_t size) { DebugBreak(); return 0; }
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
int32 const MAX_STACK_STRING_LENGTH = MAX_FRAME_LEN * MAX_STACK_DEPTH;
#else
int32 const MAX_STACK_STRING_LENGTH = 1;
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

    volatile size_t reference_alloc_number  = 0;
    const char* reference_tag               = nullptr;

    // The max_sane_alloc would ideally be 64 * 1024, but std::vector uses new
    // even when reallocating and raises too many false alerts.
    size_t max_sane_alloc                   = 256 * 1024;
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

static void dbgdeadfillpointer(void** ppv)
{
    memset(ppv, get_config().deadfill, sizeof(*ppv));
}

};

using namespace debugheap;

extern "C" char const* dbginspectmemory(void const* pv, size_t size)
{
    auto& config = get_config();

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
    size_t count_unfiltered = 0;
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

    bool ascii_string = true;
    bool update_string = true;
    bool unicode_ascii_string = !(DWORD_PTR(bytes) & 1);
    bool unicode_update_string = unicode_ascii_string;
    while (size--)
    {
        *(hex++) = c_hex[*bytes >> 4];
        *(hex++) = c_hex[*bytes & 0x0f];
        *(hex++) = ' ';

        if (size && !((chr - s_chr) & 1))
        {
            const wchar_t w = bytes[0] | (bytes[1] << 8);
            if (unicode_update_string)
            {
                if (!w)
                    unicode_update_string = false;
                else if (w < 32 || w > 126)
                    unicode_ascii_string = false;
            }
        }

        if (*bytes >= 32 && *bytes <= 126)
        {
            *(chr++) = *bytes;
            count_unfiltered++;
        }
        else
        {
            *(chr++) = '.';
            if (!*bytes)
            {
                update_string = false;
            }
            else if (*bytes != config.allocfill)
            {
                if (update_string)
                    ascii_string = false;
                count_filtered++;
            }
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

    const bool string_like = (ascii_string || unicode_ascii_string) && (count_filtered <= (max / 2));
    return string_like ? s_chr : s_hex;
}

inline BYTE* byteptr(const void* pv)
{
    return const_cast<BYTE*>(static_cast<const BYTE*>(pv));
}

size_t mem_tracking::pad_size(size_t size)
{
    assert(size);
    return sizeof(mem_tracking) + get_config().size_guard * 2 + size;
}

mem_tracking* mem_tracking::from_pv(const void* pv)
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

    if (m_prev)
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

char const* mem_tracking::get_label()
{
    if (m_label)
        return m_label;

#ifdef USE_RTTI
    if (m_flags & memObject)
        return typeid(*static_cast<object*>(to_pv())).name();
#endif

    if (m_flags & memNewArray)
        return "new[]";
    if (m_flags & memNew)
        return "new";

    return "malloc";
}

void mem_tracking::set_label(char const* const label, bool const own)
{
    m_label = label;
    m_own_label = label && own;
}

#ifdef INCLUDE_CALLSTACKS
void mem_tracking::get_stack_string(char* buffer, size_t max, bool newlines) const
{
    format_frames(_countof(m_stack), m_stack, m_hash, buffer, max, newlines);
}
#endif

extern "C" void dbgsetsanealloc(size_t max_alloc, size_t max_realloc, const size_t *exceptions )
{
    auto& config = get_config();
    config.max_sane_alloc = max_alloc;
    config.max_sane_realloc = max_realloc;
    config.sane_alloc_exceptions = exceptions;
}

extern "C" void* dbgalloc_(size_t size, uint32 flags)
{
#ifdef INCLUDE_CALLSTACKS
    DWORD hash;
    void* stack[MAX_STACK_DEPTH];
    const int32 skip = 1 + !!(flags & memSkipOneFrame) + !!(flags &memSkipAnotherFrame);
    size_t const levels = (flags & memNoStack) ? 0 : get_callstack_frames(skip, _countof(stack), stack, &hash);
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
    p->m_thread = GetCurrentThreadId();

#ifdef INCLUDE_CALLSTACKS
    static_assert(sizeof(p->m_stack) == sizeof(stack), "mismatched stack array size");
    static_assert(sizeof(p->m_stack[0]) == sizeof(stack[0]), "mismatched stack frame size");
    memcpy(p->m_stack, stack, levels * sizeof(stack[0]));
    if (levels < _countof(p->m_stack))
        p->m_stack[levels] = 0;
    p->m_hash = hash;
#endif

    // Link the block into the list.

    p->link();

    // Return pointer to caller-data portion of the block.

    return p->to_pv();
}

void* dbgrealloc_(void* pv, size_t size, uint32 flags)
{
    if (!size)
    {
        dbgfree_(pv, flags);
        return nullptr;
    }

    if (!pv)
        return dbgalloc_(size, flags | memSkipAnotherFrame);

#ifdef INCLUDE_CALLSTACKS
    DWORD hash;
    void* stack[MAX_STACK_DEPTH];
    const int32 skip = 1 + !!(flags & memSkipOneFrame) + !!(flags &memSkipAnotherFrame);
    size_t levels = get_callstack_frames(skip, _countof(stack), stack, &hash);
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

    p->m_alloc_number = dbgnewallocnumber();
    p->m_count_realloc++;
    p->m_requested_size = size;
    p->m_thread = GetCurrentThreadId();

#ifdef INCLUDE_CALLSTACKS
    static_assert(sizeof(p->m_stack) == sizeof(stack), "mismatched stack array size");
    static_assert(sizeof(p->m_stack[0]) == sizeof(stack[0]), "mismatched stack frame size");
    memcpy(p->m_stack, stack, levels * sizeof(stack[0]));
    p->m_hash = hash;
#endif

    // Link the block into the list.

    p->link();

    // Return pointer to caller-data portion of the block.

    return p->to_pv();
}

void dbgfree_(void* pv, uint32 type)
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

struct heap_info
{
    size_t count_all = 0;
    size_t size_all = 0;

    size_t count_snapshot = 0;
    size_t size_snapshot = 0;

    size_t count_ignored = 0;
    size_t size_ignored = 0;
};

static heap_info list_leaks(size_t alloc_number, bool report, bool all_leaks=false)
{
    auto_lock lock;
    auto& config = get_config();

    const bool newlines = false;

    if (!alloc_number)
        all_leaks = false;

    if (report)
    {
        if (alloc_number)
            dbgtracef("----- Checking for leaks since #%zu -----", alloc_number);
        else
            dbgtracef("----- Checking for leaks -----");
    }

    heap_info info;
    const DWORD thread = GetCurrentThreadId();
    char stack[MAX_STACK_STRING_LENGTH];
    char tid[32];

    const size_t ref = config.reference_alloc_number;
    bool show_reference = report && ref > 0;
    for (mem_tracking* p = config.head; p; p = p->m_next)
    {
        const bool after = (p->m_alloc_number > alloc_number);

        if (!all_leaks)
        {
            info.count_all++;
            info.size_all += p->m_requested_size;
        }

        if (p->m_flags & (memMarked|memIgnoreLeak))
        {
            if (!all_leaks || after)
            {
                info.count_ignored++;
                info.size_ignored += p->m_requested_size;
            }
            continue;
        }

        if (!after && !all_leaks)
            continue;

        if (all_leaks)
        {
            info.count_all++;
            info.size_all += p->m_requested_size;
        }

        if (after)
        {
            info.count_snapshot++;
            info.size_snapshot += p->m_requested_size;
        }

        if (report)
        {
            if (show_reference && ref > p->m_alloc_number)
            {
                dbgtracef("----- #%zu%s -----", ref, config.reference_tag ? config.reference_tag : "");
                show_reference = false;
            }

            tid[0] = '\0';
            if (thread != p->m_thread)
                sprintf_s(tid, _countof(tid), "THREAD %u,  ", p->m_thread);

            stack[0] = '\0';
#ifdef INCLUDE_CALLSTACKS
            if (p->m_stack[0])
                p->get_stack_string(stack, _countof(stack), newlines);
#endif
            dbgtracef("Leak:  #%zu,  %s%zu bytes (%u reallocs),  0x%p,  (%s),  %s%s%s",
                    p->m_alloc_number, tid, p->m_requested_size, p->m_count_realloc,
                    p->to_pv(), p->get_label(), dbginspectmemory(p->to_pv(), p->m_requested_size),
                    (!stack[0] ? "" : newlines ? "\r\n" : ",  context: "), stack);
        }
    }

    if (report)
    {
        if (show_reference)
            dbgtracef("----- #%zu%s -----", ref, config.reference_tag ? config.reference_tag : "");

        if (!alloc_number)
            dbgtracef("----- all: %zu allocations, %zu bytes -----", info.count_all, info.size_all);
        else if (!all_leaks)
            dbgtracef("----- snapshot: %zu allocations, %zu bytes -----", info.count_snapshot, info.size_snapshot);
        else
            dbgtracef("----- snapshot: %zu allocations, %zu bytes / all leaks: %zu allocations, %zu bytes -----",
                      info.count_snapshot, info.size_snapshot,
                      info.count_all - info.count_snapshot, info.size_all - info.size_snapshot);
    }

    return info;
}

static void reset_checker()
{
    auto_lock lock;
    auto& config = get_config();

    for (mem_tracking* p = config.head; p; p = p->m_next)
    {
        if (p->m_flags & memMarked)
            p->m_flags ^= memMarked;
    }

    config.reference_alloc_number = 0;
    config.reference_tag = nullptr;
}

extern "C" void dbgsetlabel(const void* pv, char const* label, int32 copy)
{
    auto_lock lock;

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

extern "C" void dbgverifylabel(const void* pv, char const* label)
{
    auto_lock lock;

    mem_tracking* const p = mem_tracking::from_pv(pv);

    if (p)
        assert(strcmp(label, p->get_label()) == 0);
}

extern "C" void dbgsetignore(const void* pv, int32 ignore)
{
    auto_lock lock;

    mem_tracking* const p = mem_tracking::from_pv(pv);

    if ((p->m_flags & memIgnoreLeak) != (ignore ? memIgnoreLeak : 0))
        p->m_flags ^= memIgnoreLeak;
}

extern "C" void dbgmarkmem(const void* pv)
{
    auto_lock lock;

    mem_tracking* const p = mem_tracking::from_pv(pv);
    p->m_flags |= memMarked;
}

extern "C" size_t dbggetallocnumber()
{
    auto_lock lock;
    return get_config().alloc_number;
}

extern "C" void dbgsetreference(size_t alloc_number, const char* tag)
{
    auto_lock lock;
    auto& config = get_config();

    config.reference_alloc_number = alloc_number;
    config.reference_tag = tag;
}

extern "C" void dbgcheck()
{
    dbgchecksince(0);
}

extern "C" void dbgchecksince(size_t alloc_number, int32 include_all)
{
    auto_lock lock;

    const heap_info info = list_leaks(alloc_number, false);
    if (info.count_snapshot)
    {
        if (alloc_number)
            assert4(false, "%zu leaks detected (%zu bytes).\r\n%zu total allocations (%zu bytes).",
                    info.count_snapshot, info.size_snapshot, info.count_all, info.size_all);
        else
            assert2(false, "%zu leaks detected (%zu bytes).", info.count_all, info.size_all);
        list_leaks(alloc_number, true, !!include_all);
    }

    reset_checker();
}

extern "C" size_t dbgignoresince(size_t alloc_number, size_t* total_bytes, char const* label, int32 all_threads)
{
    auto_lock lock;
    auto& config = get_config();

    size_t ignored = 0;
    size_t size = 0;

    const DWORD thread = GetCurrentThreadId();
    for (mem_tracking* p = config.head; p; p = p->m_next)
    {
        // Optimization:  Break out once past the range of interest; the list is
        // sorted by allocation number.
        if (p->m_alloc_number <= alloc_number)
            break;

        if (p->m_flags & memIgnoreLeak)
            continue;

        if (!all_threads && thread != p->m_thread)
            continue;

        p->m_flags |= memIgnoreLeak;
        if (label)
            p->set_label(label, false);

        ++ignored;
        size += p->m_requested_size;
    }

    if (total_bytes)
        *total_bytes = size;
    return ignored;
}

extern "C" void dbgcheckfinal()
{
    auto_lock lock;

    const heap_info info = list_leaks(0, true);
    if (info.count_all)
    {
        assert2(false, "%zu leaks detected (%zu bytes).", info.count_all, info.size_all);
        list_leaks(0, true);
    }

    reset_checker();
}

#ifdef USE_HEAP_STATS
// TODO: report heap stats
#endif // USE_HEAP_STATS

_Ret_notnull_ _Post_writable_byte_size_(size) DECLALLOCATOR
void* __cdecl operator new(size_t size)
{
    return dbgalloc_(size, memNew|memSkipOneFrame);
}

void __cdecl operator delete(void* pv)
{
    if (pv)
        dbgfree_(pv _MEM_NEW);
}

_Ret_notnull_ _Post_writable_byte_size_(size) DECLALLOCATOR
void* __cdecl operator new[](size_t size)
{
    return dbgalloc_(size, memNewArray|memSkipOneFrame);
}

void __cdecl operator delete[](void* pv)
{
    if (pv)
        dbgfree_(pv _MEM_NEWARRAY);
}

#ifdef USE_RTTI
void* __cdecl object::operator new(size_t size)
{
    return dbgalloc_(size, memObject|memNew);
}
#endif // USE_RTTI

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
