#include "test_framework.h"

int g_tests_run = 0;
int g_tests_failed = 0;

int test_framework_summary(void)
{
    printf("\n%d tests run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
