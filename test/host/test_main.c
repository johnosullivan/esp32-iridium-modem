#include <stdio.h>

#include "test_framework.h"

extern void run_sbd_parser_tests(void);
extern void run_uart_framing_tests(void);
extern void run_serial_replay_tests(void);

int main(void)
{
    printf("iridium host tests\n\n");
    run_sbd_parser_tests();
    printf("\n");
    run_uart_framing_tests();
    printf("\n");
    run_serial_replay_tests();
    return test_framework_summary();
}
