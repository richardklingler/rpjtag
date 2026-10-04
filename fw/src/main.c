#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "DAP_config.h"
#include "DAP.h"
#include "dap_jtag_io.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define USB_VID 0x1209u
#define USB_PID 0x5306u

#define UART_TX_PIN 0u
#define UART_RX_PIN 1u
#define JTAG_TCK_PIN 2u
#define JTAG_TMS_PIN 3u
#define JTAG_TDI_PIN 4u
#define JTAG_TDO_PIN 5u
#define JTAG_BUFFER_OE_PIN 6u
#define JTAG_BUFFER_DIR_PIN 7u
#define SRST_DRIVE_PIN 8u
#define SRST_SENSE_PIN 9u
#define TRST_DRIVE_PIN 10u
#define SPI_SCK_PIN 12u
#define SPI_MOSI_PIN 13u
#define SPI_MISO_PIN 14u
#define SPI_CS_PIN 15u
#define SPI_BUFFER_OE_PIN 16u
#define LED_STATUS_PIN 18u
#define LED_ACTIVITY_PIN 19u
#define TARGET_POWER_ENABLE_PIN 20u
#define BUTTON_PIN 21u
#define VTREF_ADC_PIN 26u
#define VTREF_ADC_INPUT 0u
#define TARGET_CURRENT_ADC_PIN 27u
#define TARGET_CURRENT_ADC_INPUT 1u

static void board_init(void) {
    gpio_init(LED_STATUS_PIN);
    gpio_set_dir(LED_STATUS_PIN, GPIO_OUT);
    gpio_put(LED_STATUS_PIN, 0);

    gpio_init(LED_ACTIVITY_PIN);
    gpio_set_dir(LED_ACTIVITY_PIN, GPIO_OUT);
    gpio_put(LED_ACTIVITY_PIN, 0);

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    adc_init();
    adc_gpio_init(VTREF_ADC_PIN);
    adc_select_input(VTREF_ADC_INPUT);
}

static float read_vtref_mv(void) {
    const uint16_t raw = adc_read();
    const float vref = (float)raw * 3.3f / 4095.0f;
    return vref * 2000.0f;
}

static void handle_usb_command(const char *command) {
    if (strcmp(command, "INFO") == 0) {
        printf("RPJTAG_INFO fw_version=0x0100 hw_rev=0x01 vtref_mv=%.0f button_pressed=%u\n",
               read_vtref_mv(),
               !gpio_get(BUTTON_PIN) ? 1u : 0u);
    } else if (strcmp(command, "VTREF") == 0) {
        printf("RPJTAG_VTREF mv=%.0f\n", read_vtref_mv());
    } else if (strcmp(command, "IDCODE") == 0) {
        const uint32_t idcode = dap_jtag_scan_idcode();
        printf("RPJTAG_IDCODE idcode=0x%08lx\n", (unsigned long)idcode);
    } else if (strncmp(command, "TCK ", 4) == 0) {
        char *end = NULL;
        const unsigned long requested_hz = strtoul(command + 4, &end, 10);
        if (end == command + 4 || *end != '\0' || requested_hz > UINT32_MAX ||
            !dap_jtag_set_tck((uint32_t)requested_hz)) {
            puts("RPJTAG_ERROR invalid_tck");
        } else {
            printf("RPJTAG_TCK hz=%lu\n", requested_hz);
        }
    } else {
        puts("RPJTAG_ERROR unknown_command");
    }
}

static void poll_usb_commands(void) {
    static char command[32];
    static size_t command_length;
    static bool command_overflowed;

    int character;
    while ((character = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (character == '\r' || character == '\n') {
            if (command_overflowed) {
                puts("RPJTAG_ERROR command_too_long");
            } else if (command_length > 0) {
                command[command_length] = '\0';
                handle_usb_command(command);
            }
            command_length = 0;
            command_overflowed = false;
        } else if (command_length < sizeof(command) - 1) {
            command[command_length++] = (char)character;
        } else {
            command_overflowed = true;
        }
    }
}

static void poll_cmsis_dap(void) {
    static uint8_t request[DAP_PACKET_SIZE];
    static uint8_t response[DAP_PACKET_SIZE];

    while (tud_vendor_available() != 0) {
        const uint32_t request_length = tud_vendor_read(request, sizeof(request));
        if (request_length == 0) {
            break;
        }

        const uint32_t response_length = DAP_ExecuteCommand(request, response) & 0xffffu;
        uint32_t written = 0;
        while (written < response_length) {
            const uint32_t count = tud_vendor_write(response + written, response_length - written);
            if (count == 0) {
                tud_task();
                continue;
            }
            written += count;
        }
        tud_vendor_write_flush();
    }
}

int main(void) {
    tusb_init();
    stdio_init_all();
    board_init();
    DAP_Setup();

    puts("rpjtag firmware M0 bring-up ready");
    printf("USB VID: 0x%04x PID: 0x%04x\n", USB_VID, USB_PID);
    puts("Board: Pico 2 W, RP2350A");

    uint32_t blink_counter = 0;
    while (true) {
        tud_task();
        poll_cmsis_dap();
        poll_usb_commands();

        const bool button_pressed = !gpio_get(BUTTON_PIN);
        const float vtref_mv = read_vtref_mv();

        gpio_put(LED_STATUS_PIN, button_pressed ? 1 : 0);
        gpio_put(LED_ACTIVITY_PIN, (blink_counter & 1u) ? 1 : 0);

        printf("blink=%lu vtref_mv=%.0f button=%d\n",
               (unsigned long)blink_counter,
               vtref_mv,
               button_pressed ? 1 : 0);

        sleep_ms(250);
        blink_counter++;
    }

    return 0;
}
