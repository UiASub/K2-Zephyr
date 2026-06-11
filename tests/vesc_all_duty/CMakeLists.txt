cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k2_vesc_all_duty_test)

target_sources(app PRIVATE
    src/main.c
    ../../src/vesc/vesc_protocol.c
)

target_include_directories(app PRIVATE ../../src)
