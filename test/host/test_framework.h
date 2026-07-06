#ifndef TEST_FRAMEWORK_H_INCLUDED
#define TEST_FRAMEWORK_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_tests_run;
extern int g_tests_failed;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        int _failed_before = g_tests_failed;                                \
        printf("  %s ... ", #name);                                           \
        g_tests_run++;                                                        \
        name();                                                               \
        if (g_tests_failed == _failed_before) {                             \
            printf("ok\n");                                                   \
        } else {                                                              \
            printf("FAILED\n");                                               \
        }                                                                     \
    } while (0)

#define ASSERT_TRUE(expr)                                                     \
    do {                                                                      \
        if (!(expr)) {                                                        \
            g_tests_failed++;                                                 \
            fprintf(stderr, "\n    FAIL %s:%d: expected true: %s\n",          \
                    __FILE__, __LINE__, #expr);                               \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _exp = (long long)(expected);                               \
        long long _act = (long long)(actual);                                 \
        if (_exp != _act) {                                                   \
            g_tests_failed++;                                                 \
            fprintf(stderr, "\n    FAIL %s:%d: expected %lld got %lld\n",     \
                    __FILE__, __LINE__, _exp, _act);                          \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_STR_EQ(expected, actual)                                       \
    do {                                                                      \
        const char *_exp = (expected);                                        \
        const char *_act = (actual);                                          \
        if (_exp == NULL || _act == NULL || strcmp(_exp, _act) != 0) {        \
            g_tests_failed++;                                                 \
            fprintf(stderr, "\n    FAIL %s:%d: expected '%s' got '%s'\n",     \
                    __FILE__, __LINE__,                                       \
                    _exp != NULL ? _exp : "(null)",                           \
                    _act != NULL ? _act : "(null)");                          \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_OK(expr) ASSERT_TRUE(expr)

int test_framework_summary(void);

#endif /* TEST_FRAMEWORK_H_INCLUDED */
