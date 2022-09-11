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
#if defined(PLATFORM_WINDOWS)
#   define PATH_SEP "\\"
#else
#   define PATH_SEP "/"
#endif

//------------------------------------------------------------------------------
#define FILE_LINE __FILE__ "(" AS_STR(__LINE__) "): "
#if !defined(_MSC_VER) || defined(HIDE_TODO)
#define TODO(s)
#define WARNING(s)
#else
#define TODO(s) __pragma(message (FILE_LINE /*"warning: "*/ "TODO: " s))
#define WARNING(s) __pragma(message (FILE_LINE /*"warning: "*/ "WARN: " s))
#endif
//#define PRAGMA_ERROR(s) __pragma(message (FILE_LINE "error: " s))

//------------------------------------------------------------------------------
extern const char* get_bindable_esc();

//------------------------------------------------------------------------------
#if defined(DEBUG)
int dbg_get_env_int(const char* name, int default=0);
void dbg_printf_row(int row, const char* fmt, ...);
void dbg_printf(const char* fmt, ...);
#endif

//------------------------------------------------------------------------------
struct no_copy
{
            no_copy() = default;
            ~no_copy() = default;

private:
            no_copy(const no_copy&) = delete;
            no_copy(no_copy&&) = delete;
    no_copy& operator = (const no_copy&) = delete;
    no_copy& operator = (no_copy&&) = delete;
};

//------------------------------------------------------------------------------
template <class T> class rollback : public no_copy
{
public:
            rollback(T& var) : m_var(var), m_rollback(var) {}
            rollback(T& var, const T value) : rollback(var) { m_var = value; }
            ~rollback() { if (m_var != m_rollback) m_var = m_rollback; }

    void    reset() { if (m_var != m_rollback) m_var = m_rollback; }

private:
    T&      m_var;
    const T m_rollback;
};

//------------------------------------------------------------------------------
// Remove top-level const qualifier.
template <class T> struct remove_const { using type = T; };
template <class T> struct remove_const<const T> { using type = T; };
template <class T> using remove_const_t = typename remove_const<T>::type;

//------------------------------------------------------------------------------
template <class T> struct autoptr
{
    autoptr(T* p) : m_p(p) {}
    autoptr(const autoptr<T>& other) = delete;
    autoptr(autoptr<T>&& other) { m_p = other.m_p; other.m_p = nullptr; }
    ~autoptr() { free(const_cast<remove_const_t<T>*>(m_p)); }
    autoptr<T>& operator=(const autoptr<T>& other) = delete;
    autoptr<T>& operator=(autoptr<T>&& other) { m_p = other.m_p; other.m_p = nullptr; }
    T** operator&() const { return const_cast<T**>(&m_p); }
private:
    T* m_p;
};

//------------------------------------------------------------------------------
template <class T> inline void suppress_unused_var(T var) {}
