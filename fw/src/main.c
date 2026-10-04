#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "jtag_transfer.pio.h"

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

#define JTAG_SCAN_CYCLE_COUNT 48u
#define JTAG_SCAN_DATA_OFFSET 10u
#define JTAG_TX_WORD_COUNT 3u

static uint jtag_sm;
static uint jtag_program_offset;
static int jtag_tx_dma;
static int jtag_rx_dma;
static uint32_t jtag_tck_hz = 1000000u;

static float jtag_clock_divider(void) {
    return (float)clock_get_hz(clk_sys) / (10.0f * (float)jtag_tck_hz);
}

static void jtag_init(void) {
    jtag_sm = pio_claim_unused_sm(pio0, true);
    jtag_program_offset = pio_add_program(pio0, &jtag_transfer_program);

    pio_sm_config config = jtag_transfer_program_get_default_config(jtag_program_offset);
    sm_config_set_sideset_pins(&config, JTAG_TCK_PIN);
    sm_config_set_out_pins(&config, JTAG_TMS_PIN, 2);
    sm_config_set_in_pins(&config, JTAG_TDO_PIN);
    sm_config_set_out_shift(&config, true, true, 32);
    sm_config_set_in_shift(&config, false, true, 1);
    sm_config_set_clkdiv(&config, jtag_clock_divider());

    for (uint pin = JTAG_TCK_PIN; pin <= JTAG_TDO_PIN; ++pin) {
        pio_gpio_init(pio0, pin);
    }

    pio_sm_init(pio0, jtag_sm, jtag_program_offset, &config);
    pio_sm_set_consecutive_pindirs(pio0, jtag_sm, JTAG_TCK_PIN, 1, true);
    pio_sm_set_consecutive_pindirs(pio0, jtag_sm, JTAG_TMS_PIN, 2, true);
    pio_sm_set_consecutive_pindirs(pio0, jtag_sm, JTAG_TDO_PIN, 1, false);
    pio_sm_set_pins_with_mask(pio0, jtag_sm, 0,
                              (1u << JTAG_TCK_PIN) |
                              (1u << JTAG_TMS_PIN) |
                              (1u << JTAG_TDI_PIN));

    jtag_tx_dma = dma_claim_unused_channel(true);
    jtag_rx_dma = dma_claim_unused_channel(true);
}

static void board_init(void) {
    gpio_init(LED_STATUS_PIN);
    gpio_set_dir(LED_STATUS_PIN, GPIO_OUT);
    gpio_put(LED_STATUS_PIN, 0);

    gpio_init(LED_ACTIVITY_PIN);
    gpio_set_dir(LED_ACTIVITY_PIN, GPIO_OUT);
    gpio_put(LED_ACTIVITY_PIN, 0);

    jtag_init();

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

static bool jtag_set_tck(uint32_t requested_hz) {
    const uint32_t max_tck_hz = clock_get_hz(clk_sys) / 10u;
    if (requested_hz < 1000u || requested_hz > max_tck_hz) {
        return false;
    }
    jtag_tck_hz = requested_hz;
    return true;
}

static uint32_t jtag_scan_idcode(void) {
    uint32_t tx_words[JTAG_TX_WORD_COUNT] = {0};
    uint32_t rx_samples[JTAG_SCAN_CYCLE_COUNT] = {0};
    uint32_t idcode = 0;

    for (uint32_t cycle = 0; cycle < JTAG_SCAN_CYCLE_COUNT; ++cycle) {
        const bool reset = cycle < 6;
        const bool enter_dr = cycle == 7;
        const bool exit_shift = cycle == JTAG_SCAN_DATA_OFFSET + 31;
        const bool update_dr = cycle == JTAG_SCAN_DATA_OFFSET + 32;
        const bool tms = reset || enter_dr || exit_shift || update_dr;
        const uint32_t tx_bit = cycle * 2;
        if (tms) {
            tx_words[tx_bit / 32] |= 1u << (tx_bit % 32);
        }
    }

    pio_sm_clear_fifos(pio0, jtag_sm);
    pio_sm_restart(pio0, jtag_sm);
    pio_sm_set_clkdiv(pio0, jtag_sm, jtag_clock_divider());
    pio_sm_set_enabled(pio0, jtag_sm, true);

    dma_channel_config_t rx_config = dma_channel_get_default_config((uint)jtag_rx_dma);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_dreq(&rx_config, pio_get_dreq(pio0, jtag_sm, false));
    dma_channel_configure((uint)jtag_rx_dma, &rx_config, rx_samples,
                          &pio0->rxf[jtag_sm], JTAG_SCAN_CYCLE_COUNT, false);

    dma_channel_config_t tx_config = dma_channel_get_default_config((uint)jtag_tx_dma);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, pio_get_dreq(pio0, jtag_sm, true));
    dma_channel_configure((uint)jtag_tx_dma, &tx_config, &pio0->txf[jtag_sm],
                          tx_words, JTAG_TX_WORD_COUNT, false);

    dma_start_channel_mask(1u << (uint)jtag_rx_dma);
    dma_start_channel_mask(1u << (uint)jtag_tx_dma);
    dma_channel_wait_for_finish_blocking((uint)jtag_rx_dma);
    dma_channel_wait_for_finish_blocking((uint)jtag_tx_dma);
    pio_sm_set_enabled(pio0, jtag_sm, false);

    for (uint32_t bit_index = 0; bit_index < 32; ++bit_index) {
        if ((rx_samples[JTAG_SCAN_DATA_OFFSET + bit_index] & 1u) != 0) {
            idcode |= 1u << bit_index;
        }
    }

    return idcode;
}

static void handle_usb_command(const char *command) {
    if (strcmp(command, "INFO") == 0) {
        printf("RPJTAG_INFO fw_version=0x0100 hw_rev=0x01 vtref_mv=%.0f button_pressed=%u\n",
               read_vtref_mv(),
               !gpio_get(BUTTON_PIN) ? 1u : 0u);
    } else if (strcmp(command, "VTREF") == 0) {
        printf("RPJTAG_VTREF mv=%.0f\n", read_vtref_mv());
    } else if (strcmp(command, "IDCODE") == 0) {
        const uint32_t idcode = jtag_scan_idcode();
        printf("RPJTAG_IDCODE idcode=0x%08lx\n", (unsigned long)idcode);
    } else if (strncmp(command, "TCK ", 4) == 0) {
        char *end = NULL;
        const unsigned long requested_hz = strtoul(command + 4, &end, 10);
        if (end == command + 4 || *end != '\0' || requested_hz > UINT32_MAX ||
            !jtag_set_tck((uint32_t)requested_hz)) {
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

int main(void) {
    stdio_init_all();
    board_init();

    puts("rpjtag firmware M0 bring-up ready");
    printf("USB VID: 0x%04x PID: 0x%04x\n", USB_VID, USB_PID);
    puts("Board: Pico 2 W, RP2350A");

    uint32_t blink_counter = 0;
    while (true) {
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
