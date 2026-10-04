#include <stdio.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define USB_VID 0x1209u
#define USB_PID 0x5306u

#define LED_STATUS_PIN 24u
#define LED_ACTIVITY_PIN 25u
#define VTREF_ADC_PIN 40u
#define BUTTON_PIN 21u

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
    adc_select_input(0);
}

static float read_vtref_mv(void) {
    const uint16_t raw = adc_read();
    const float vref = (float)raw * 3.3f / 4095.0f;
    return vref * 2000.0f;
}

int main(void) {
    stdio_init_all();
    board_init();

    puts("rpjtag firmware M0 bring-up ready");
    printf("USB VID: 0x%04x PID: 0x%04x\n", USB_VID, USB_PID);
    puts("Board: Pico 2 W, RP2354B");

    uint32_t blink_counter = 0;
    while (true) {
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
