#!/usr/bin/env bash
set -euo pipefail

MCU_IP="${MCU_IP:-10.77.0.2}"
MCU_PORT="${MCU_PORT:-1337}"
IMAGE="${IMAGE:-build-h755-ota/K2-Zephyr/zephyr/zephyr.signed.bin}"
TIMEOUT="${TIMEOUT:-10}"
TRIES="${TRIES:-2}"
POLL_SECONDS="${POLL_SECONDS:-30}"
RESET=1

usage() {
    cat <<EOF
Usage: $0 [options] [image]

Upload a signed MCUboot image over Ethernet with MCUmgr, mark it for test boot,
reset the MCU, and show the post-boot image state.

Options:
  --host IP          MCU IP address (default: ${MCU_IP})
  --port PORT        MCUmgr UDP port (default: ${MCU_PORT})
  --timeout SEC      MCUmgr timeout per try (default: ${TIMEOUT})
  --tries N          MCUmgr tries per command (default: ${TRIES})
  --poll-seconds N   Seconds to wait for the MCU after reset (default: ${POLL_SECONDS})
  --no-reset         Upload and mark test, but do not reset the MCU
  -h, --help         Show this help

Environment overrides: MCU_IP, MCU_PORT, IMAGE, TIMEOUT, TRIES, POLL_SECONDS, MCUMGR
EOF
}

while (($#)); do
    case "$1" in
        --host)
            MCU_IP="${2:?missing value for --host}"
            shift 2
            ;;
        --port)
            MCU_PORT="${2:?missing value for --port}"
            shift 2
            ;;
        --timeout)
            TIMEOUT="${2:?missing value for --timeout}"
            shift 2
            ;;
        --tries)
            TRIES="${2:?missing value for --tries}"
            shift 2
            ;;
        --poll-seconds)
            POLL_SECONDS="${2:?missing value for --poll-seconds}"
            shift 2
            ;;
        --no-reset)
            RESET=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            IMAGE="$1"
            shift
            ;;
    esac
done

find_mcumgr() {
    if [[ -n "${MCUMGR:-}" ]]; then
        echo "$MCUMGR"
        return
    fi
    if command -v mcumgr >/dev/null 2>&1; then
        command -v mcumgr
        return
    fi
    if [[ -x "${HOME}/go/bin/mcumgr" ]]; then
        echo "${HOME}/go/bin/mcumgr"
        return
    fi
    echo "mcumgr not found. Install it with:" >&2
    echo "  sudo dnf install golang" >&2
    echo "  go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest" >&2
    echo "Then make sure ~/go/bin is on PATH." >&2
    exit 1
}

MCUMGR_BIN="$(find_mcumgr)"

if [[ ! -f "$IMAGE" ]]; then
    echo "Image not found: $IMAGE" >&2
    echo "Build one first, for example: ./build.sh --h7 --ota" >&2
    exit 1
fi

mcumgr() {
    "$MCUMGR_BIN" \
        --conntype udp \
        "--connstring=[${MCU_IP}]:${MCU_PORT}" \
        --timeout "$TIMEOUT" \
        --tries "$TRIES" \
        "$@"
}

slot_hash() {
    local slot="$1"
    awk -v slot="$slot" '
        $0 ~ "image=[0-9]+ slot=" slot { in_slot = 1; next }
        /^[[:space:]]*image=/ { in_slot = 0 }
        in_slot && /^[[:space:]]*hash:/ { print $2; exit }
    '
}

echo "MCU: ${MCU_IP}:${MCU_PORT}"
echo "Image: ${IMAGE}"
echo

echo "Current image state:"
mcumgr image list
echo

echo "Uploading signed image..."
mcumgr image upload "$IMAGE"
echo

echo "Image state after upload:"
list_after_upload="$(mcumgr image list)"
printf '%s\n' "$list_after_upload"
echo

test_hash="$(printf '%s\n' "$list_after_upload" | slot_hash 1)"
if [[ -z "$test_hash" ]]; then
    echo "Could not find slot 1 hash after upload; refusing to reset." >&2
    exit 1
fi

echo "Marking slot 1 image for test boot:"
echo "  ${test_hash}"
mcumgr image test "$test_hash"
echo

if (( RESET == 0 )); then
    echo "Skipping reset because --no-reset was supplied."
    exit 0
fi

echo "Resetting MCU..."
mcumgr reset || true
echo

echo "Waiting for MCU to come back..."
deadline=$((SECONDS + POLL_SECONDS))
while (( SECONDS < deadline )); do
    if final_list="$(mcumgr image list 2>/dev/null)"; then
        printf '%s\n' "$final_list"
        exit 0
    fi
    sleep 2
done

echo "Warning: MCU did not respond to MCUmgr within ${POLL_SECONDS}s after reset." >&2
echo "Check power, Ethernet link, and the direct-link network profile." >&2
exit 1
