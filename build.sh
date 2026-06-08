#!/usr/bin/env bash

# Zephyr Build Script
# This script sets up the environment and builds the K2-Zephyr project.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace_dir="$(dirname "$script_dir")"
board="nucleo_h755zi_q/stm32h755xx/m7"
board_label="H7 (nucleo_h755zi_q/stm32h755xx/m7)"
ota_build=1

usage() {
    echo "Usage: ./build.sh [--h7|--H7] [--ota|--OTA] [--no-ota|--NO-OTA]"
    echo "Defaults to the H7 OTA build."
    echo "Use --no-ota only for a plain non-MCUboot development build."
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
            ;;
        --ota|--OTA)
            ota_build=1
            ;;
        --no-ota|--NO-OTA)
            ota_build=0
            ;;
        --f7|--F7)
            echo "F7 support has been sunset. Use the H7 target: ${board}" >&2
            exit 1
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
    build_dir="build-h755-ota"
    echo "Building K2-Zephyr OTA image for ${board_label}..."
    west build --sysbuild -p -b "$board" -d "$build_dir" "$script_dir" -- "-DEXTRA_CONF_FILE=ota.conf"
    echo "OTA build complete for ${board_label}."
    echo "Signed image: ${build_dir}/*/zephyr/zephyr.signed.bin"
    print_flash_guidance
else
    build_dir="build-h755"
    echo "Building K2-Zephyr non-OTA image for ${board_label}..."
    west build -p -b "$board" -d "$build_dir" "$script_dir"
    echo "Non-OTA build complete for ${board_label}."
    echo "Use west flash -d ${build_dir} only when you intentionally need USB flashing."
fi
