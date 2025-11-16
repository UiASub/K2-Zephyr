#!/usr/bin/env bash

# Zephyr Build Script
# This script sets up the environment and builds the K2-Zephyr project

set -e  # Exit on error

echo "Setting up Zephyr environment..."
cd ~/zephyrproject || exit 1

# shellcheck source=/dev/null
source .venv/bin/activate

echo "Building K2-Zephyr project..."
cd ~/zephyrproject/K2-Zephyr || exit 1
west build -p -b nucleo_f767zi

echo "Build complete! Flash with: west flash"