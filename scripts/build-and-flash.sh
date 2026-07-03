#!/usr/bin/env bash
set -euo pipefail

SOURCE="${BASH_SOURCE[0]}"
while [[ -h "$SOURCE" ]]; do
  DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/../examples"
MONITOR=false
SKIP_BUILD=false

usage() {
  cat <<'EOF'
Usage: build-and-flash.sh [options]

Options:
  -m, --monitor     Open serial monitor after flashing
  -s, --skip-build  Flash only (skip idf.py build)
  -h, --help        Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -m|--monitor) MONITOR=true; shift ;;
    -s|--skip-build) SKIP_BUILD=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "${IDF_PATH:-}" ]]; then
  for candidate in \
    "$HOME/esp/v5.3.1/esp-idf/export.sh" \
    "$HOME/esp/esp-idf/export.sh" \
    "$HOME/esp-idf/export.sh"
  do
    if [[ -f "$candidate" ]]; then
      # shellcheck disable=SC1090
      source "$candidate"
      break
    fi
  done
fi

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "ESP-IDF not found. Source export.sh first, e.g.:" >&2
  echo "  source ~/esp/esp-idf/export.sh" >&2
  exit 1
fi

cd "$PROJECT_DIR"

if [[ "$SKIP_BUILD" == false ]]; then
  echo "==> Building..."
  idf.py build
fi

PYTHON=python
if ! command -v "$PYTHON" >/dev/null 2>&1; then
  PYTHON=python3
fi

PORTS=()
while IFS= read -r line; do
  PORTS+=("$line")
done < <(
  "$PYTHON" - <<'PY'
import sys

try:
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial not available — source ESP-IDF export.sh first")

ports = []
for p in list_ports.comports():
    dev = p.device
    if sys.platform == "darwin" and dev.startswith("/dev/tty."):
        dev = "/dev/cu." + dev[len("/dev/tty."):]
    ports.append((dev, p.description or "unknown"))

seen = set()
for dev, desc in sorted(ports, key=lambda x: x[0]):
    if dev in seen:
        continue
    seen.add(dev)
    print(f"{dev}\t{desc}")
PY
)

if [[ ${#PORTS[@]} -eq 0 ]]; then
  echo "No serial devices found. Plug in the ESP32 and try again." >&2
  exit 1
fi

DEVICES=()
DESCRIPTIONS=()
for line in "${PORTS[@]}"; do
  DEVICES+=("${line%%$'\t'*}")
  DESCRIPTIONS+=("${line#*$'\t'}")
done

# ESP32 native USB on macOS appears as /dev/cu.usbmodem*
USBMODEM_DEVICES=()
USBMODEM_DESCRIPTIONS=()
for i in "${!DEVICES[@]}"; do
  if [[ "${DEVICES[$i]}" == /dev/cu.usbmodem* ]]; then
    USBMODEM_DEVICES+=("${DEVICES[$i]}")
    USBMODEM_DESCRIPTIONS+=("${DESCRIPTIONS[$i]}")
  fi
done

PORT=""
if [[ ${#USBMODEM_DEVICES[@]} -eq 1 ]]; then
  PORT="${USBMODEM_DEVICES[0]}"
  echo "==> ESP32 USB device: ${PORT} (${USBMODEM_DESCRIPTIONS[0]})"
elif [[ ${#USBMODEM_DEVICES[@]} -gt 1 ]]; then
  echo "==> Multiple ESP32 USB devices found:"
  PS3="Select device to flash: "
  select _choice in "${USBMODEM_DEVICES[@]}"; do
    if [[ -n "${_choice:-}" ]]; then
      PORT="$_choice"
      break
    fi
    echo "Invalid selection, try again."
  done
elif [[ ${#DEVICES[@]} -eq 1 ]]; then
  PORT="${DEVICES[0]}"
  echo "==> One device found: ${PORT} (${DESCRIPTIONS[0]})"
else
  echo "==> Multiple serial devices found:"
  PS3="Select device to flash: "
  select _choice in "${DEVICES[@]}"; do
    if [[ -n "${_choice:-}" ]]; then
      PORT="$_choice"
      break
    fi
    echo "Invalid selection, try again."
  done
fi

echo "==> Flashing to ${PORT}..."
if [[ "$MONITOR" == true ]]; then
  idf.py -p "$PORT" flash monitor
else
  idf.py -p "$PORT" flash
fi
