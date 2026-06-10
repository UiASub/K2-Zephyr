#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace_dir="$(dirname "$script_dir")"
board="nucleo_f767zi"
board_label="F7 (nucleo_f767zi)"
build_dir="build-f767-esc-debug"

usage() {
    echo "Usage: ./build.sh [--f7|--F7|--h7|--H7|--esp32-supermini]"
    echo "Builds the minimal ESC UART debug firmware. Defaults to F7."
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --h7|--H7)
            board="nucleo_h755zi_q/stm32h755xx/m7"
            board_label="H7 (nucleo_h755zi_q/stm32h755xx/m7)"
            build_dir="build-h755-esc-debug"
            ;;
        --f7|--F7)
            board="nucleo_f767zi"
            board_label="F7 (nucleo_f767zi)"
            build_dir="build-f767-esc-debug"
            ;;
        --esp32-supermini|--esp32c3-supermini|--esp32|--ESP32)
            board="esp32c3_supermini"
            board_label="ESP32-C3 SuperMini (esp32c3_supermini)"
            build_dir="build-esp32c3-supermini-esc-debug"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

echo "Setting up Zephyr environment..."

if [[ -f "${workspace_dir}/.venv/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "${workspace_dir}/.venv/bin/activate"
elif ! command -v west >/dev/null 2>&1; then
    echo "west not found. Install west or create a Zephyr virtual environment at ${workspace_dir}/.venv." >&2
    exit 1
fi

cd "$script_dir" || exit 1
echo "Building minimal ESC debug firmware for ${board_label}..."
west build -p -b "$board" -d "$build_dir" "$script_dir"
echo "ESC debug build complete for ${board_label}."
echo "Flash with: west flash -d ${build_dir}"
