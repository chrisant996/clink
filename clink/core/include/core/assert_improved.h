// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#ifdef DEBUG

#define assert0(expr, fmt)                      ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt), 0)))
#define assert1(expr, fmt, a)                   ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt, a), 0)))
#define assert2(expr, fmt, a, b)                ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt, a, b), 0)))
#define assert3(expr, fmt, a, b, c)             ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt, a, b, c), 0)))
#define assert4(expr, fmt, a, b, c, d)          ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt, a, b, c, d), 0)))
#define assert5(expr, fmt, a, b, c, d, e)       ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt, a, b, c, d, e), 0)))
#define assert6(expr, fmt, a, b, c, d, e, f)    ((void)(!!(expr) || (dbgassertf(__FILE__, __LINE__, fmt, a, b, c, d, e, f), 0)))

#ifdef __cplusplus
extern "C" {
#endif

void dbgassertf(const char* file, unsigned line, const char* fmt, ...);
void dbgtracef(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#else // !DEBUG

#define assert0(expr, fmt)                      ((void)0)
#define assert1(expr, fmt, a)                   ((void)0)
#define assert2(expr, fmt, a, b)                ((void)0)
#define assert3(expr, fmt, a, b, c)             ((void)0)
#define assert4(expr, fmt, a, b, c, d)          ((void)0)
#define assert5(expr, fmt, a, b, c, d, e)       ((void)0)
#define assert6(expr, fmt, a, b, c, d, e, f)    ((void)0)

#endif // !DEBUG
