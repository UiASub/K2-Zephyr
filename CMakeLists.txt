# CMake build configuration for Zephyr RTOS application
# This file tells the build system how to compile our embedded application

# Require minimum CMake version for modern features
cmake_minimum_required(VERSION 3.20.0)

# Export compile_commands.json for IDE IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Make the local dts/ tree (bindings and overlays) visible to the DTS compiler.
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Find and load the Zephyr RTOS build system
# ZEPHYR_BASE environment variable points to Zephyr installation
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Define our project name (used in build outputs)
project(k2_zephyr_app)

# Use OpenOCD (from Zephyr SDK) as default flash/debug runner
set(BOARD_FLASH_RUNNER openocd)
set(BOARD_DEBUG_RUNNER openocd)

# Tell Zephyr to compile our main source file into the application
# 'app' is Zephyr's standard target name for the main application binary
target_sources(app PRIVATE src/main.c
                           src/led.c
                           src/net.c
                           src/control.c
                           src/icm20948.c
                           src/resource_monitor.c)
