#ifndef __TEST_WILDMATCH_H__
#define __TEST_WILDMATCH_H__

#include <stdio.h>

#define test_fail(x) {{ \
    fprintf(stderr, "error: %s:%d\n", __FILE__, __LINE__); \
    return x; \
}}

#define assert_true(expr) {{ \
    if (!(expr)) { \
        test_fail(1); \
    } \
}}

#define assert_equal(a, b) assert_true((a) == (b))
#define assert_not_equal(a, b) assert_true((a) != (b))

#endif
