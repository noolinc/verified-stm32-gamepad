/* check.h - minimal assertion helpers for host unit tests. */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int check_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);      \
            check_failures++;                                                    \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long va_ = (long long)(a), vb_ = (long long)(b);                    \
        if (va_ != vb_) {                                                        \
            printf("%s:%d: CHECK_EQ failed: %s == %s (%lld vs %lld)\n",          \
                   __FILE__, __LINE__, #a, #b, va_, vb_);                        \
            check_failures++;                                                    \
        }                                                                        \
    } while (0)

static inline int check_done(const char *name)
{
    if (check_failures != 0) {
        printf("%s: %d failure(s)\n", name, check_failures);
        return 1;
    }
    printf("%s: ok\n", name);
    return 0;
}

#endif
