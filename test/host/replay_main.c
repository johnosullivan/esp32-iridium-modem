#include <stdio.h>
#include <stdlib.h>

#include "../../iridium_sim.h"

static void print_usage(const char *prog)
{
    printf("Usage: %s <fixture.txt>\n\n", prog);
    printf("Replay a recorded Iridium modem transcript as serial traffic\n");
    printf("and print the parsed device state.\n\n");
    printf("Example:\n");
    printf("  %s test/host/fixtures/mo_send_success.txt\n", prog);
}

static void print_state(const iridium_sim_state_t *state)
{
    printf("Parsed state\n");
    printf("  OK lines      : %d\n", state->ok_count);
    printf("  Parse errors  : %d\n", state->parse_errors);
    printf("  Ring alerts   : %d\n", state->ring_alerts);
    printf("  MO status     : %d\n", state->mo_status);
    printf("  MOMSN         : %d\n", state->momsn);
    printf("  MT status     : %d\n", state->mt_status);
    printf("  MTMSN         : %d\n", state->mtmsn);
    printf("  MT length     : %d\n", state->mt_length);
    printf("  MT queued     : %d\n", state->mt_queued);
    printf("  Last payload  : %s\n", state->last_payload);
    printf("  Device TX log :\n%s\n", state->tx_log);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    iridium_sim_script_t script = {0};
    iridium_sim_uart_stack_t stack = {0};
    iridium_sim_state_t state = {0};

    if (!iridium_sim_load_fixture(argv[1], &script)) {
        fprintf(stderr, "Failed to load fixture: %s\n", argv[1]);
        return 1;
    }

    iridium_sim_state_init(&state);

    printf("Replaying fixture: %s\n", argv[1]);
    printf("  exchanges     : %zu\n", script.exchange_count);
    printf("  unsolicited   : %zu\n", script.unsolicited.line_count);
    printf("\n");

    for (size_t i = 0; i < script.exchange_count; i++) {
        const iridium_sim_exchange_t *exchange = &script.exchanges[i];
        char payload[IRI_SIM_MAX_LINE_LEN * IRI_SIM_MAX_LINES] = {0};

        printf("--- Exchange %zu ---\n", i + 1);
        printf("TX: %s\r\n", exchange->tx_command);
        iridium_sim_format_serial_payload(&exchange->rx, payload, sizeof(payload));
        printf("RX: %s", payload);
    }

    printf("\n");
    if (!iridium_sim_replay_script(&stack, &state, &script)) {
        fprintf(stderr, "Replay failed\n");
        return 1;
    }

    print_state(&state);
    return state.parse_errors == 0 ? 0 : 2;
}
