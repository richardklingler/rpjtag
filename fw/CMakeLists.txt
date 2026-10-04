cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w)
set(PICO_SDK_PATH "/Users/klingler/Nextcloud/Develop/Pico/pico-sdk")

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(rpjtag_firmware C CXX ASM)

pico_sdk_init()

add_executable(rpjtag_firmware
    src/main.c
)

target_include_directories(rpjtag_firmware PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(rpjtag_firmware PRIVATE
    USBD_VID=0x1209
    USBD_PID=0x5306
    USBD_DESC_STR_MAX=21
    USBD_MANUFACTURER=\"Klingler\\040Engineering\"
    USBD_PRODUCT=\"rpJTAG\"
)

target_link_libraries(rpjtag_firmware
    pico_stdlib
    pico_stdio_usb
    hardware_adc
    hardware_gpio
)

pico_enable_stdio_usb(rpjtag_firmware 1)
pico_enable_stdio_uart(rpjtag_firmware 0)
pico_add_extra_outputs(rpjtag_firmware)
