# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

set(CARD_NAME "cowork")
project(${CARD_NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Firmware flash reserve — must match docs/FORMATS.md §1 (256 KB).
set(COWORK_FIRMWARE_RESERVE 262144)

add_executable(${CARD_NAME}
    src/main.cpp
    src/ComputerCardImpl.cpp
    src/CoWorkCard.cpp
    src/link/MidiLink.cpp
    src/storage/Banks.cpp
    src/storage/ConfigStore.cpp
    src/storage/FlashOps.cpp
    src/storage/UploadManager.cpp
    src/usb/usb_core1.cpp
    src/usb/usb_descriptors.c
    src/usb/usb_midi_host.c
    src/usb/usb_midi_host_app_driver.c
)

target_compile_options(${CARD_NAME} PRIVATE
    -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

target_compile_definitions(${CARD_NAME} PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    COWORK_FIRMWARE_RESERVE=${COWORK_FIRMWARE_RESERVE}u
)

# tusb_config.h lives in src/usb; ComputerCard.h and the rest in src/
target_include_directories(${CARD_NAME} PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
)

target_link_libraries(${CARD_NAME}
    pico_stdlib
    pico_multicore
    pico_unique_id
    hardware_adc
    hardware_dma
    hardware_flash
    hardware_i2c
    hardware_pwm
    hardware_spi
    hardware_sync
    hardware_vreg
    tinyusb_device
    tinyusb_host
    tinyusb_board
)

# Run from RAM: flash writes at runtime must not stall code fetch
# (audio keeps running through sample uploads; XIP data reads are gated
# separately by the quiesce handshake).
pico_set_binary_type(${CARD_NAME} copy_to_ram)

# Custom composite descriptors + tud_cdc_*, not pico stdio USB
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_enable_stdio_uart(${CARD_NAME} 0)

pico_add_extra_outputs(${CARD_NAME})

# Fail the build if the binary outgrows its flash reserve, then keep a
# copy of the UF2 in UF2/ (repo convention).
add_custom_command(TARGET ${CARD_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DBIN_FILE="$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.bin"
        -DFIRMWARE_RESERVE=${COWORK_FIRMWARE_RESERVE}
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/check_firmware_reserve.cmake"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_LIST_DIR}/UF2"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:${CARD_NAME}>/${CARD_NAME}.uf2"
        "${CMAKE_CURRENT_LIST_DIR}/UF2/${CARD_NAME}.uf2"
)
