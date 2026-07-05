#include "test_framework.h"
#include "../../iridium_parser.h"

TEST(test_finalize_ok_sbdix_response_only)
{
    /* Modem returns result line then OK; stack pop order is top-first. */
    const char *stack[] = {"+SBDIX: 0,12,1,8,50,0"};
    char command[IRI_PARSER_AT_CMD_MAX] = {0};
    char data[IRI_PARSER_RESPONSE_MAX] = {0};

    ASSERT_OK(iridium_uart_finalize_ok(stack, 1, command, sizeof(command),
                                       data, sizeof(data)));
    ASSERT_STR_EQ("", command);
    ASSERT_STR_EQ("+SBDIX: 0,12,1,8,50,0", data);
}

TEST(test_finalize_ok_with_echoed_command)
{
    const char *stack[] = {"+SBDIX: 0,1,1,2,25,0", "AT+SBDIX"};
    char command[IRI_PARSER_AT_CMD_MAX] = {0};
    char data[IRI_PARSER_RESPONSE_MAX] = {0};

    ASSERT_OK(iridium_uart_finalize_ok(stack, 2, command, sizeof(command),
                                       data, sizeof(data)));
    ASSERT_STR_EQ("AT+SBDIX", command);
    ASSERT_STR_EQ("+SBDIX: 0,1,1,2,25,0", data);

    iridium_sbd_session_t session = {0};
    ASSERT_OK(iridium_parser_sbd_session(data, &session));
    ASSERT_EQ(25, session.mt_length);
}

TEST(test_finalize_ok_sbdrt_payload)
{
    const char *stack[] = {"Hello from space", "AT+SBDRT"};
    char command[IRI_PARSER_AT_CMD_MAX] = {0};
    char data[IRI_PARSER_RESPONSE_MAX] = {0};

    ASSERT_OK(iridium_uart_finalize_ok(stack, 2, command, sizeof(command),
                                       data, sizeof(data)));
    ASSERT_STR_EQ("AT+SBDRT", command);
    ASSERT_STR_EQ("Hello from space", data);
}

TEST(test_fixture_mt_ring_session)
{
    /* fixtures/mt_ring_session.txt — SBDRING follow-up read */
    const char *stack[] = {"payload-abc123", "AT+SBDRT"};
    char command[IRI_PARSER_AT_CMD_MAX] = {0};
    char data[IRI_PARSER_RESPONSE_MAX] = {0};

    ASSERT_OK(iridium_uart_finalize_ok(stack, 2, command, sizeof(command),
                                       data, sizeof(data)));
    ASSERT_STR_EQ("AT+SBDRT", command);
    ASSERT_STR_EQ("payload-abc123", data);
}

void run_uart_framing_tests(void)
{
    printf("UART framing tests\n");
    RUN_TEST(test_finalize_ok_sbdix_response_only);
    RUN_TEST(test_finalize_ok_with_echoed_command);
    RUN_TEST(test_finalize_ok_sbdrt_payload);
    RUN_TEST(test_fixture_mt_ring_session);
}
