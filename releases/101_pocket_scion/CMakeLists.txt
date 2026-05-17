cmake_minimum_required(VERSION 3.13)

# Use PICO_SDK_PATH from environment or set explicitly:
#   cmake -DPICO_SDK_PATH=~/projects/rp2040/pico-sdk-2.1 ..
include(pico_sdk_import.cmake)

project(pocket_scion C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(pocket_scion)

target_sources(pocket_scion PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/pocket_scion.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_midi_host_app_driver.c
)

# Include current directory (tusb_config.h, placeholder_spatial.h, etc.)
# and ComputerCard library directory.
target_include_directories(pocket_scion PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../Demonstrations+HelloWorlds/PicoSDK/ComputerCard
)

# Avoid startup timing problems
target_compile_definitions(pocket_scion PUBLIC
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_definitions(pocket_scion PRIVATE
    PICO_DEFAULT_UART_BAUD_RATE=115200
)

pico_enable_stdio_uart(pocket_scion 0)
pico_enable_stdio_usb(pocket_scion 0)
pico_add_extra_outputs(pocket_scion)

target_compile_options(pocket_scion PRIVATE
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)
target_link_options(pocket_scion PRIVATE -Wl,--print-memory-usage)

target_link_libraries(pocket_scion
    tinyusb_host
    tinyusb_board
    pico_unique_id
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_flash
)
