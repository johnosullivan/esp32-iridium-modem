#include "test_framework.h"
#include "../../iridium_sim.h"

TEST(test_load_mo_send_success_fixture)
{
    iridium_sim_script_t script = {0};
    ASSERT_OK(iridium_sim_load_fixture("fixtures/mo_send_success.txt", &script));
    ASSERT_EQ(2, (int)script.exchange_count);

    const iridium_sim_exchange_t *exchange = NULL;
    ASSERT_OK(iridium_sim_find_exchange(&script, "AT+SBDWT=hello", &exchange));
    ASSERT_EQ(1, (int)exchange->rx.line_count);
    ASSERT_STR_EQ("OK", exchange->rx.lines[0]);

    ASSERT_OK(iridium_sim_find_exchange(&script, "AT+SBDIX", &exchange));
    ASSERT_EQ(2, (int)exchange->rx.line_count);
    ASSERT_STR_EQ("+SBDIX: 0,42,1,17,25,0", exchange->rx.lines[0]);
    ASSERT_STR_EQ("OK", exchange->rx.lines[1]);
}

TEST(test_replay_mo_send_success)
{
    iridium_sim_script_t script = {0};
    iridium_sim_uart_stack_t stack = {0};
    iridium_sim_state_t state = {0};

    iridium_sim_state_init(&state);
    ASSERT_OK(iridium_sim_load_fixture("fixtures/mo_send_success.txt", &script));
    ASSERT_OK(iridium_sim_replay_script(&stack, &state, &script));

    ASSERT_EQ(2, state.ok_count);
    ASSERT_EQ(0, state.parse_errors);
    ASSERT_EQ(0, state.mo_status);
    ASSERT_EQ(42, state.momsn);
    ASSERT_EQ(1, state.mt_status);
    ASSERT_EQ(25, state.mt_length);
    ASSERT_TRUE(iridium_parser_mo_transfer_ok(state.mo_status));
}

TEST(test_replay_mo_retry_no_network)
{
    iridium_sim_script_t script = {0};
    iridium_sim_uart_stack_t stack = {0};
    iridium_sim_state_t state = {0};

    iridium_sim_state_init(&state);
    ASSERT_OK(iridium_sim_load_fixture("fixtures/mo_retry_no_network.txt", &script));
    ASSERT_OK(iridium_sim_replay_script(&stack, &state, &script));

    ASSERT_EQ(32, state.mo_status);
    ASSERT_FALSE(iridium_parser_mo_transfer_ok(state.mo_status));
}

TEST(test_replay_mt_ring_session)
{
    iridium_sim_script_t script = {0};
    iridium_sim_uart_stack_t stack = {0};
    iridium_sim_state_t state = {0};

    iridium_sim_state_init(&state);
    ASSERT_OK(iridium_sim_load_fixture("fixtures/mt_ring_session.txt", &script));
    ASSERT_EQ(1, (int)script.unsolicited.line_count);
    ASSERT_STR_EQ("SBDRING", script.unsolicited.lines[0]);
    ASSERT_OK(iridium_sim_replay_script(&stack, &state, &script));

    ASSERT_EQ(1, state.ring_alerts);
    ASSERT_STR_EQ("payload-abc123", state.last_payload);
    ASSERT_EQ(14, state.mt_length);
}

TEST(test_feed_raw_serial_payload)
{
    iridium_sim_uart_stack_t stack = {0};
    iridium_sim_state_t state = {0};

    iridium_sim_state_init(&state);
    iridium_sim_device_tx(&state, "AT+SBDIX");

    const char *payload = "+SBDIX: 0,12,1,8,50,0\r\nOK\r\n";
    ASSERT_OK(iridium_sim_feed_bytes(&stack, &state, payload));

    ASSERT_EQ(0, state.mo_status);
    ASSERT_EQ(50, state.mt_length);
    ASSERT_EQ(1, state.ok_count);
}

TEST(test_format_serial_payload)
{
    iridium_sim_response_t response = {0};
    iridium_parser_copy_string(response.lines[0], sizeof(response.lines[0]),
                               "+SBDIX: 0,1,1,2,25,0");
    iridium_parser_copy_string(response.lines[1], sizeof(response.lines[1]), "OK");
    response.line_count = 2;

    char payload[128] = {0};
    size_t len = iridium_sim_format_serial_payload(&response, payload, sizeof(payload));
    ASSERT_TRUE(len > 0);
    ASSERT_STR_EQ("+SBDIX: 0,1,1,2,25,0\r\nOK\r\n", payload);
}

void run_serial_replay_tests(void)
{
    printf("Serial replay tests\n");
    RUN_TEST(test_load_mo_send_success_fixture);
    RUN_TEST(test_replay_mo_send_success);
    RUN_TEST(test_replay_mo_retry_no_network);
    RUN_TEST(test_replay_mt_ring_session);
    RUN_TEST(test_feed_raw_serial_payload);
    RUN_TEST(test_format_serial_payload);
}
