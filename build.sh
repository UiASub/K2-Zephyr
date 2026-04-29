#!/usr/bin/env bash

# Zephyr Build Script
# This script sets up the environment and builds the K2-Zephyr project

set -e  # Exit on error

board="nucleo_h755zi_q/stm32h755xx/m7"
board_label="H7 (nucleo_h755zi_q/stm32h755xx/m7)"

usage() {
    echo "Usage: ./build.sh [--h7|--H7|--f7|--F7]"
    echo "Defaults to H7 if no board flag is provided."
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --h7|--H7)
            board="nucleo_h755zi_q/stm32h755xx/m7"
            board_label="H7 (nucleo_h755zi_q/stm32h755xx/m7)"
            ;;
        --f7|--F7)
            board="nucleo_f767zi"
            board_label="F7 (nucleo_f767zi)"
            ;;
        --help)
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
cd ~/zephyrproject || exit 1

if [[ -f .venv/bin/activate ]]; then
    # shellcheck source=/dev/null
    source .venv/bin/activate
elif ! command -v west >/dev/null 2>&1; then
    echo "west not found. Install west or create ~/zephyrproject/.venv." >&2
    exit 1
fi

echo "Building K2-Zephyr project for ${board_label}..."
cd ~/zephyrproject/K2-Zephyr || exit 1
west build -p -b "$board"

echo "Build complete for ${board_label}! Flash with: west flash"
