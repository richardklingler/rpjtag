#ifndef RPJTAG_DAP_CONFIG_H
#define RPJTAG_DAP_CONFIG_H

#include <stdint.h>
#include <string.h>

#include "cmsis_compiler.h"
#include "dap_jtag_io.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#define DAP_SWD 0
#define DAP_JTAG 1
#define DAP_JTAG_DEV_CNT 8
#define DAP_DEFAULT_PORT 2u
#define DAP_DEFAULT_SWJ_CLOCK 1000000u
#define DAP_PACKET_SIZE 64u
#define DAP_PACKET_COUNT 4u

#define CPU_CLOCK clock_get_hz(clk_sys)
#define IO_PORT_WRITE_CYCLES 1u
#define DELAY_FAST_CYCLES 0u
#define DELAY_SLOW_CYCLES 1u

#define SWO_UART 0
#define SWO_MANCHESTER 0
#define SWO_STREAM 0
#define DAP_UART 0
#define DAP_UART_USB_COM_PORT 0
#define TIMESTAMP_CLOCK 1000000u

#define TARGET_FIXED 0
#define DAP_SETUP() dap_jtag_init()
#define PORT_JTAG_SETUP() dap_jtag_pio_enable()
#define PORT_SWD_SETUP() ((void)0)
#define PORT_OFF() dap_jtag_pio_disable()

#define PIN_SWCLK_TCK_IN() dap_jtag_pin_input(0)
#define PIN_SWCLK_TCK_SET() dap_jtag_pin_output(0, 1)
#define PIN_SWCLK_TCK_CLR() dap_jtag_pin_output(0, 0)
#define PIN_SWDIO_TMS_IN() dap_jtag_pin_input(1)
#define PIN_SWDIO_TMS_SET() dap_jtag_pin_output(1, 1)
#define PIN_SWDIO_TMS_CLR() dap_jtag_pin_output(1, 0)
#define PIN_SWDIO_IN() 0u
#define PIN_SWDIO_OUT(bit) ((void)(bit))
#define PIN_SWDIO_OUT_ENABLE() ((void)0)
#define PIN_SWDIO_OUT_DISABLE() ((void)0)
#define PIN_TDI_IN() dap_jtag_pin_input(2)
#define PIN_TDI_OUT(bit) dap_jtag_pin_output(2, (bit))
#define PIN_TDO_IN() dap_jtag_pin_input(3)
#define PIN_nTRST_IN() 1u
#define PIN_nTRST_OUT(bit) ((void)(bit))
#define PIN_nRESET_IN() 1u
#define PIN_nRESET_OUT(bit) ((void)(bit))
#define LED_CONNECTED_OUT(bit) ((void)(bit))
#define LED_RUNNING_OUT(bit) ((void)(bit))
#define TIMESTAMP_GET() time_us_32()
#define RESET_TARGET() 0u

__STATIC_INLINE uint8_t DAP_GetVendorString(char *string) {
    static const char value[] = "Klingler Engineering";
    memcpy(string, value, sizeof(value));
    return (uint8_t)sizeof(value);
}

__STATIC_INLINE uint8_t DAP_GetProductString(char *string) {
    static const char value[] = "rpJTAG";
    memcpy(string, value, sizeof(value));
    return (uint8_t)sizeof(value);
}

__STATIC_INLINE uint8_t DAP_GetSerNumString(char *string) {
    pico_get_unique_board_id_string(string, 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
    return (uint8_t)(2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
}

__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString(char *string) {
    (void)string;
    return 0;
}

__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString(char *string) {
    (void)string;
    return 0;
}

__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString(char *string) {
    (void)string;
    return 0;
}

__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString(char *string) {
    (void)string;
    return 0;
}

__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString(char *string) {
    (void)string;
    return 0;
}

#endif