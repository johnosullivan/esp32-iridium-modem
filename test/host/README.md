# Host unit tests

Fast, hardware-free tests for Iridium SBD parsing and UART response framing.

## Run

From the repository root:

```bash
make test
```

Or directly:

```bash
make -C test/host test
```

## What's covered

- **SBD session parsing** — `+SBDIX`, `+SBDSX`, MO/MT field extraction, malformed input
- **MO success codes** — retry logic helper (`iridium_parser_mo_transfer_ok`)
- **CSQ parsing** — signal strength lines
- **UART OK framing** — stack pop order → `(command, data)` assembly

## Fixtures

Golden modem transcripts live in `fixtures/` and are referenced by tests in
`test_sbd_parser.c` and `test_uart_framing.c`. Extend these when you capture
new field traces from a RockBLOCK.

## Adding tests

1. Add a `TEST(...)` function in `test_sbd_parser.c` or `test_uart_framing.c`
2. Register it in the corresponding `run_*_tests()` function
3. Run `make test`

Pure parsing lives in `iridium_parser.c` at the repo root — no ESP-IDF required.
