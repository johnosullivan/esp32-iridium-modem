#!/usr/bin/env python3
"""
Simulate an Iridium RockBLOCK modem on a serial port.

Usage:
  # Terminal 1 — create a virtual serial pair (macOS/Linux):
  socat -d -d pty,raw,echo=0 pty,raw,echo=0

  # Terminal 2 — run the simulator on one end:
  python3 scripts/modem_sim.py /dev/ttys006

  # Point ESP32 UART at the other end (/dev/ttys005), or replay a fixture locally:
  python3 scripts/modem_sim.py --fixture test/host/fixtures/mo_send_success.txt --dry-run

Fixture format (same as test/host/fixtures/):
  AT+SBDWT=hello     # device transmits
  OK                 # modem responds
  AT+SBDIX
  +SBDIX: 0,42,1,17,25,0
  OK
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path


def load_fixture(path: Path) -> list[tuple[str, list[str]]]:
    exchanges: list[tuple[str, list[str]]] = []
    unsolicited: list[str] = []
    current_tx = ""
    current_rx: list[str] = []

    def flush() -> None:
        nonlocal current_tx, current_rx
        if current_tx:
            exchanges.append((current_tx, current_rx))
            current_tx = ""
            current_rx = []

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("AT"):
            if current_tx:
                flush()
            elif current_rx and not exchanges:
                unsolicited.extend(current_rx)
                current_rx = []
            current_tx = line
        else:
            current_rx.append(line)

    flush()
    return exchanges, unsolicited


def format_rx(lines: list[str]) -> bytes:
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def dry_run(path: Path) -> int:
    exchanges, unsolicited = load_fixture(path)
    print(f"Fixture: {path}")
    if unsolicited:
        print("Unsolicited RX:")
        print(format_rx(unsolicited).decode("ascii"))
    for idx, (tx, rx) in enumerate(exchanges, start=1):
        print(f"\n--- Exchange {idx} ---")
        print(f"Expect TX: {tx}\\r")
        print("Reply RX:")
        print(format_rx(rx).decode("ascii"), end="")
    return 0


def run_serial(port: str, baud: int, path: Path | None) -> int:
    try:
        import serial
    except ImportError:
        print("Install pyserial: pip install pyserial", file=sys.stderr)
        return 1

    exchanges: list[tuple[str, list[str]]] = []
    unsolicited: list[str] = []
    if path is not None:
        exchanges, unsolicited = load_fixture(path)

    with serial.Serial(port, baudrate=baud, timeout=0.1) as ser:
        print(f"Modem simulator listening on {port} @ {baud}")
        if unsolicited:
            ser.write(format_rx(unsolicited))
            print(f"Sent unsolicited: {unsolicited}")

        buffer = b""
        exchange_idx = 0

        while True:
            chunk = ser.read(256)
            if chunk:
                buffer += chunk

            while b"\r" in buffer:
                line, _, buffer = buffer.partition(b"\r")
                command = line.decode("ascii", errors="replace").strip()
                if not command:
                    continue

                print(f"RX <= {command}")

                if exchanges:
                    expected_tx, rx_lines = exchanges[exchange_idx]
                    if command == expected_tx or command.startswith(expected_tx):
                        payload = format_rx(rx_lines)
                        ser.write(payload)
                        print(f"TX => {payload.decode('ascii')!r}")
                        exchange_idx = min(exchange_idx + 1, len(exchanges) - 1)
                        continue

                if command == "AT":
                    ser.write(b"OK\r\n")
                elif command.startswith("AT+CSQ"):
                    ser.write(b"+CSQ: 4\r\nOK\r\n")
                elif command.startswith("AT+SBDIX"):
                    ser.write(b"+SBDIX: 0,1,1,2,25,0\r\nOK\r\n")
                elif command.startswith("AT+SBDWT"):
                    ser.write(b"OK\r\n")
                elif command.startswith("AT+SBDRT"):
                    ser.write(b"simulated-payload\r\nOK\r\n")
                else:
                    ser.write(b"ERROR\r\n")

            time.sleep(0.01)


def main() -> int:
    parser = argparse.ArgumentParser(description="Iridium modem serial simulator")
    parser.add_argument("port", nargs="?", help="Serial port (e.g. /dev/ttys006)")
    parser.add_argument("--fixture", type=Path, help="Fixture transcript to replay")
    parser.add_argument("--baud", type=int, default=19200, help="Serial baud rate")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print scripted traffic without opening a serial port",
    )
    args = parser.parse_args()

    if args.dry_run:
        if args.fixture is None:
            parser.error("--dry-run requires --fixture")
        return dry_run(args.fixture)

    if args.port is None:
        parser.error("serial port required unless --dry-run is used")

    return run_serial(args.port, args.baud, args.fixture)


if __name__ == "__main__":
    raise SystemExit(main())
