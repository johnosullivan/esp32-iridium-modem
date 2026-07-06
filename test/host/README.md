# Host unit tests & serial simulation

Fast, hardware-free tests for Iridium SBD parsing, UART framing, and recorded
modem transcript replay.

## Quick start

From the repository root:

```bash
make test
```

## Serial traffic simulation

Three ways to simulate modem serial payloads:

### 1. Host replay (fastest)

Replay a fixture through the same UART RX state machine the firmware uses:

```bash
make replay
# or
./test/host/build/iridium_sim_replay test/host/fixtures/mo_send_success.txt
```

Prints each TX/RX exchange and the parsed MO/MT state.

### 2. Unit tests (CI-friendly)

Serial replay is covered by `test_serial_replay.c`:

```bash
make -C test/host test
```

Tests load fixtures from `fixtures/` and assert parsed MO status, MT payload,
ring alerts, and raw `\r\n` byte feeding.

### 3. Python modem simulator (with ESP32 hardware)

Use a virtual serial port to act as the modem while the ESP32 runs real firmware.

**Terminal 1** — create a PTY pair:

```bash
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# note the two /dev/ttysXXX paths
```

**Terminal 2** — run the simulator on one end:

```bash
python3 scripts/modem_sim.py /dev/ttys006 \
  --fixture test/host/fixtures/mo_send_success.txt
```

**Terminal 3** — point ESP32 UART at the other `/dev/ttysXXX` port and flash/run
the example firmware.

Preview scripted traffic without hardware:

```bash
make sim-dry-run
```

## Fixture format

Fixtures are plain-text modem transcripts in `fixtures/`:

```text
# comment
AT+SBDWT=hello       # device TX (lines starting with AT)
OK                   # modem RX
AT+SBDIX
+SBDIX: 0,42,1,17,25,0
OK
```

- Lines starting with `AT` are **device transmit**
- All other non-comment lines are **modem receive**
- Lines before the first `AT` are **unsolicited** (e.g. `SBDRING`)

Capture new fixtures from a real RockBLOCK with a serial logger, then drop the
file into `fixtures/` and add a test case.

## What's covered

| Layer | Command | What it tests |
|-------|---------|---------------|
| Parser | `make test` | `+SBDIX`, `+SBDSX`, CSQ field parsing |
| Framing | `make test` | OK/stack assembly → `(command, data)` |
| Replay | `make test` / `make replay` | Full fixture TX/RX serial flow |
| Hardware | `modem_sim.py` | Live ESP32 against scripted modem |

## Adding tests

1. Add or capture a fixture in `fixtures/`
2. Add a `TEST(...)` in `test_serial_replay.c`
3. Register it in `run_serial_replay_tests()`
4. Run `make test`

Core modules:

- `iridium_parser.c` — pure AT/SBD parsing
- `iridium_sim.c` — fixture loader + UART RX replay state machine
