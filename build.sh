#!/usr/bin/env bash

# Zephyr Build Script
# This script sets up the environment and builds the K2-Zephyr project.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace_dir="$(dirname "$script_dir")"
board="nucleo_h755zi_q/stm32h755xx/m7"
board_label="H7 (nucleo_h755zi_q/stm32h755xx/m7)"
ota_build=1
ota_arg_seen=0

usage() {
    echo "Usage: ./build.sh [--h7|--H7|--f7|--F7] [--ota|--OTA] [--no-ota|--NO-OTA]"
    echo "Defaults to the H7 OTA build. F7 defaults to a plain development build."
    echo "Use --no-ota only when you intentionally need a plain non-MCUboot development build."
}

print_flash_guidance() {
    cat <<EOF

K2 flashing:
  Ignore Zephyr's generic "west flash" hint unless you are doing first-time USB
  provisioning or debug recovery.
  Normal updates should use: ./tools/k2-ota.sh
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --h7|--H7)
            board="nucleo_h755zi_q/stm32h755xx/m7"
            board_label="H7 (nucleo_h755zi_q/stm32h755xx/m7)"
            ;;
        --ota|--OTA)
            ota_build=1
            ota_arg_seen=1
            ;;
        --no-ota|--NO-OTA)
            ota_build=0
            ota_arg_seen=1
            ;;
        --f7|--F7)
            board="nucleo_f767zi"
            board_label="F7 (nucleo_f767zi)"
            if [[ "$ota_arg_seen" -eq 0 ]]; then
                ota_build=0
            fi
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
if [[ "$ota_build" -eq 1 ]]; then
    if [[ "$board" == "nucleo_f767zi" ]]; then
        build_dir="build-f767-ota"
    else
        build_dir="build-h755-ota"
    fi
    echo "Building K2-Zephyr OTA image for ${board_label}..."
    west build --sysbuild -p -b "$board" -d "$build_dir" "$script_dir" -- "-DEXTRA_CONF_FILE=ota.conf"
    echo "OTA build complete for ${board_label}."
    echo "Signed image: ${build_dir}/*/zephyr/zephyr.signed.bin"
    print_flash_guidance
else
    if [[ "$board" == "nucleo_f767zi" ]]; then
        build_dir="build-f767"
    else
        build_dir="build-h755"
    fi
    echo "Building K2-Zephyr non-OTA image for ${board_label}..."
    west build -p -b "$board" -d "$build_dir" "$script_dir"
    echo "Non-OTA build complete for ${board_label}."
    echo "Use west flash -d ${build_dir} only when you intentionally need USB flashing."
fi
