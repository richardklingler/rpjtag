cmake_minimum_required(VERSION 3.14)

set(PICO_BOARD pico2_w)
set(PICO_SDK_PATH "/Users/klingler/Nextcloud/Develop/Pico/pico-sdk")

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

include(FetchContent)
FetchContent_Declare(cmsis_dap
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS-DAP.git
    GIT_TAG 12636590eec66fae2d1bba4518749426ad5a4595
)
FetchContent_MakeAvailable(cmsis_dap)

project(rpjtag_firmware C CXX ASM)

pico_sdk_init()

add_executable(rpjtag_firmware
    src/main.c
    src/dap_jtag_io.c
    src/usb_descriptors.c
    ${cmsis_dap_SOURCE_DIR}/Firmware/Source/DAP.c
)

pico_generate_pio_header(rpjtag_firmware ${CMAKE_CURRENT_LIST_DIR}/src/jtag_transfer.pio)

target_include_directories(rpjtag_firmware PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${cmsis_dap_SOURCE_DIR}/Firmware/Include
    ${PICO_SDK_PATH}/src/rp2_common/cmsis/stub/CMSIS/Core/Include
)

target_compile_definitions(rpjtag_firmware PRIVATE
    USBD_VID=0x1209
    USBD_PID=0x5306
    USBD_MANUFACTURER=\"Klingler\\040Engineering\"
    USBD_PRODUCT=\"rpJTAG\"
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
    PICO_STDIO_USB_ENABLE_TINYUSB_INIT=0
)

target_link_libraries(rpjtag_firmware
    pico_stdlib
    pico_stdio_usb
    tinyusb_device
    hardware_adc
    hardware_clocks
    hardware_dma
    hardware_gpio
    hardware_pio
)

pico_enable_stdio_usb(rpjtag_firmware 1)
pico_enable_stdio_uart(rpjtag_firmware 0)
pico_add_extra_outputs(rpjtag_firmware)
