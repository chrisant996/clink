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

#if defined(_MSC_VER)
#   define THREAD_LOCAL     __declspec(thread)
#elif defined(__GNUC__)
#   define THREAD_LOCAL     __thread
#endif

#if defined(_M_AMD64) || defined(__x86_64__)
#   define ARCHITECTURE     x64
#elif defined(_M_IX86) || defined(__i386)
#   define ARCHITECTURE     x86
#else
#   error Unknown architecture
#endif

#undef min
template <class A> A min(A a, A b) { return (a < b) ? a : b; }

#undef max
template <class A> A max(A a, A b) { return (a > b) ? a : b; }
