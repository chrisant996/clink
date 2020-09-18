// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#define sizeof_array(x)     (sizeof(x) / sizeof(x[0]))
#define AS_STR(x)           AS_STR_IMPL(x)
#define AS_STR_IMPL(x)      #x

#if defined(_WIN32)
#   define PLATFORM_WINDOWS
#else
#   error Unsupported platform.
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#   define threadlocal      __declspec(thread)
#else
#   define threadlocal      thread_local
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#   define align_to(x)       __declspec(align(x))
#else
#   define align_to(x)      alignas(x)
#endif

#if defined(_M_AMD64) || defined(__x86_64__)
#   define ARCHITECTURE     64
#elif defined(_M_IX86) || defined(__i386)
#   define ARCHITECTURE     86
#else
#   error Unknown architecture
#endif

#undef min
template <class A> A min(A a, A b) { return (a < b) ? a : b; }

#undef max
template <class A> A max(A a, A b) { return (a > b) ? a : b; }

#undef clamp
template <class A> A clamp(A v, A m, A M) { return min(max(v, m), M); }

//------------------------------------------------------------------------------
struct no_copy
{
            no_copy() = default;
            ~no_copy() = default;

private:
            no_copy(const no_copy&) = delete;
            no_copy(const no_copy&&) = delete;
    void    operator = (const no_copy&) = delete;
    void    operator = (const no_copy&&) = delete;
};

//------------------------------------------------------------------------------
// Build options:

// Define this to enable ChrisAnt modifications that should eventually be guarded by some kind of
// setting, but first I'm just getting them basically working.
#define CLINK_CHRISANT_MODS

// Define this to enable ChrisAnt fixes that might interfere with expected behavior.
#define CLINK_CHRISANT_FIXES

#ifdef CLINK_CHRISANT_MODS
#define CLINK_049_API_COMPAT
#endif

