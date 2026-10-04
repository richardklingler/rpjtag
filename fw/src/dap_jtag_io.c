#include <string.h>

#include "DAP_config.h"
#include "DAP.h"
#include "dap_jtag_io.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "jtag_transfer.pio.h"

#define JTAG_TCK_PIN 2u
#define JTAG_TMS_PIN 3u
#define JTAG_TDI_PIN 4u
#define JTAG_TDO_PIN 5u
#define JTAG_MAX_SEQUENCE_BITS 64u
#define JTAG_MAX_SEQUENCE_BYTES (JTAG_MAX_SEQUENCE_BITS / 8u)
#define JTAG_TX_WORD_COUNT 5u
#define JTAG_PIO_CYCLES_PER_TCK 3u
#define JTAG_MIN_TCK_HZ 10000u
#define JTAG_MAX_TCK_HZ 30000000u
#define JTAG_SCAN_CYCLE_COUNT 48u
#define JTAG_SCAN_DATA_OFFSET 10u

static uint jtag_sm;
static uint jtag_program_offset;
static int jtag_tx_dma;
static int jtag_rx_dma;
static bool jtag_initialized;
static pio_sm_config jtag_config;
static uint32_t jtag_tx_words[JTAG_TX_WORD_COUNT];
static uint32_t jtag_rx_samples[JTAG_MAX_SEQUENCE_BITS];

static uint32_t jtag_requested_tck_hz(void) {
    uint32_t half_period_cycles;
    if (DAP_Data.fast_clock) {
        half_period_cycles = IO_PORT_WRITE_CYCLES + DELAY_FAST_CYCLES;
    } else {
        half_period_cycles = IO_PORT_WRITE_CYCLES +
            (uint32_t)DAP_Data.clock_delay * DELAY_SLOW_CYCLES;
    }
    if (half_period_cycles == 0) {
        half_period_cycles = 1;
    }
    const uint32_t frequency_hz = clock_get_hz(clk_sys) / (2u * half_period_cycles);
    if (frequency_hz < JTAG_MIN_TCK_HZ) {
        return JTAG_MIN_TCK_HZ;
    }
    if (frequency_hz > JTAG_MAX_TCK_HZ) {
        return JTAG_MAX_TCK_HZ;
    }
    return frequency_hz;
}

static void jtag_configure_pio_pins(void) {
    for (uint pin = JTAG_TCK_PIN; pin <= JTAG_TDO_PIN; ++pin) {
        pio_gpio_init(pio0, pin);
    }
    pio_sm_init(pio0, jtag_sm, jtag_program_offset, &jtag_config);
    pio_sm_set_consecutive_pindirs(pio0, jtag_sm, JTAG_TCK_PIN, 1, true);
    pio_sm_set_consecutive_pindirs(pio0, jtag_sm, JTAG_TMS_PIN, 2, true);
    pio_sm_set_consecutive_pindirs(pio0, jtag_sm, JTAG_TDO_PIN, 1, false);
    pio_sm_set_pins_with_mask(pio0, jtag_sm, 0,
                              (1u << JTAG_TCK_PIN) |
                              (1u << JTAG_TMS_PIN) |
                              (1u << JTAG_TDI_PIN));
}

void dap_jtag_init(void) {
    if (jtag_initialized) {
        return;
    }

    jtag_sm = pio_claim_unused_sm(pio0, true);
    jtag_program_offset = pio_add_program(pio0, &jtag_transfer_program);
    jtag_config = jtag_transfer_program_get_default_config(jtag_program_offset);
    sm_config_set_sideset_pins(&jtag_config, JTAG_TCK_PIN);
    sm_config_set_out_pins(&jtag_config, JTAG_TMS_PIN, 2);
    sm_config_set_in_pins(&jtag_config, JTAG_TDO_PIN);
    sm_config_set_out_shift(&jtag_config, true, true, 32);
    sm_config_set_in_shift(&jtag_config, false, true, 1);
    jtag_tx_dma = dma_claim_unused_channel(true);
    jtag_rx_dma = dma_claim_unused_channel(true);
    jtag_initialized = true;
    dap_jtag_pio_enable();
}

void dap_jtag_pio_enable(void) {
    if (!jtag_initialized) {
        return;
    }
    pio_sm_set_enabled(pio0, jtag_sm, false);
    jtag_configure_pio_pins();
    pio_sm_set_clkdiv(pio0, jtag_sm,
                      (float)clock_get_hz(clk_sys) /
                      ((float)JTAG_PIO_CYCLES_PER_TCK * (float)jtag_requested_tck_hz()));
    pio_sm_set_enabled(pio0, jtag_sm, true);
}

void dap_jtag_pio_disable(void) {
    if (jtag_initialized) {
        pio_sm_set_enabled(pio0, jtag_sm, false);
    }
}

bool dap_jtag_set_tck(uint32_t frequency_hz) {
    if (frequency_hz < JTAG_MIN_TCK_HZ || frequency_hz > JTAG_MAX_TCK_HZ) {
        return false;
    }

    const uint32_t fast_clock_limit = clock_get_hz(clk_sys) /
        (2u * (IO_PORT_WRITE_CYCLES + DELAY_FAST_CYCLES));
    if (frequency_hz >= fast_clock_limit) {
        DAP_Data.fast_clock = 1;
        DAP_Data.clock_delay = 1;
    } else {
        DAP_Data.fast_clock = 0;
        uint32_t delay = (clock_get_hz(clk_sys) / 2u + frequency_hz - 1u) / frequency_hz;
        if (delay > IO_PORT_WRITE_CYCLES) {
            delay -= IO_PORT_WRITE_CYCLES;
            delay = (delay + DELAY_SLOW_CYCLES - 1u) / DELAY_SLOW_CYCLES;
        } else {
            delay = 1;
        }
        DAP_Data.clock_delay = delay;
    }

    return true;
}

static void jtag_set_sio_mode(void) {
    dap_jtag_pio_disable();
    for (uint pin = JTAG_TCK_PIN; pin <= JTAG_TDO_PIN; ++pin) {
        gpio_set_function(pin, GPIO_FUNC_SIO);
    }
    gpio_set_dir(JTAG_TCK_PIN, GPIO_OUT);
    gpio_set_dir(JTAG_TMS_PIN, GPIO_OUT);
    gpio_set_dir(JTAG_TDI_PIN, GPIO_OUT);
    gpio_set_dir(JTAG_TDO_PIN, GPIO_IN);
}

uint32_t dap_jtag_pin_input(unsigned int pin_index) {
    jtag_set_sio_mode();
    static const uint pins[] = {JTAG_TCK_PIN, JTAG_TMS_PIN, JTAG_TDI_PIN, JTAG_TDO_PIN};
    return gpio_get(pins[pin_index]);
}

void dap_jtag_pin_output(unsigned int pin_index, uint32_t value) {
    jtag_set_sio_mode();
    static const uint pins[] = {JTAG_TCK_PIN, JTAG_TMS_PIN, JTAG_TDI_PIN};
    if (pin_index < sizeof(pins) / sizeof(pins[0])) {
        gpio_put(pins[pin_index], value != 0);
    }
}

static void jtag_run_cycles(uint32_t count, const uint8_t *tms_bits,
                            const uint8_t *tdi_bits, uint8_t *tdo_bits) {
    if (count == 0 || count > JTAG_MAX_SEQUENCE_BITS) {
        return;
    }

    memset(jtag_tx_words, 0, sizeof(jtag_tx_words));
    memset(jtag_rx_samples, 0, count * sizeof(jtag_rx_samples[0]));
    jtag_tx_words[0] = count - 1u;
    if (tdo_bits != NULL) {
        memset(tdo_bits, 0, (count + 7u) / 8u);
    }

    for (uint32_t bit = 0; bit < count; ++bit) {
        const uint32_t word_bit = 32u + bit * 2u;
        const bool tms = ((tms_bits[bit / 8u] >> (bit % 8u)) & 1u) != 0;
        const bool tdi = tdi_bits != NULL &&
            ((tdi_bits[bit / 8u] >> (bit % 8u)) & 1u) != 0;
        if (tms) {
            jtag_tx_words[word_bit / 32u] |= 1u << (word_bit % 32u);
        }
        if (tdi) {
            jtag_tx_words[(word_bit + 1u) / 32u] |= 1u << ((word_bit + 1u) % 32u);
        }
    }

    dap_jtag_pio_enable();
    pio_sm_clear_fifos(pio0, jtag_sm);
    pio_sm_restart(pio0, jtag_sm);
    pio_sm_set_clkdiv(pio0, jtag_sm,
                      (float)clock_get_hz(clk_sys) /
                      ((float)JTAG_PIO_CYCLES_PER_TCK * (float)jtag_requested_tck_hz()));

    dma_channel_config_t rx_config = dma_channel_get_default_config((uint)jtag_rx_dma);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_dreq(&rx_config, pio_get_dreq(pio0, jtag_sm, false));
    dma_channel_configure((uint)jtag_rx_dma, &rx_config, jtag_rx_samples,
                          &pio0->rxf[jtag_sm], count, false);

    const uint32_t tx_word_count = 1u + (count * 2u + 31u) / 32u;
    dma_channel_config_t tx_config = dma_channel_get_default_config((uint)jtag_tx_dma);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, pio_get_dreq(pio0, jtag_sm, true));
    dma_channel_configure((uint)jtag_tx_dma, &tx_config, &pio0->txf[jtag_sm],
                          jtag_tx_words, tx_word_count, false);

    dma_start_channel_mask(1u << (uint)jtag_rx_dma);
    dma_start_channel_mask(1u << (uint)jtag_tx_dma);
    dma_channel_wait_for_finish_blocking((uint)jtag_rx_dma);
    dma_channel_wait_for_finish_blocking((uint)jtag_tx_dma);
    pio_sm_set_enabled(pio0, jtag_sm, false);

    if (tdo_bits != NULL) {
        for (uint32_t bit = 0; bit < count; ++bit) {
            if ((jtag_rx_samples[bit] & 1u) != 0) {
                tdo_bits[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
            }
        }
    }
}

static void jtag_run_constant_tms(uint32_t count, bool tms, const uint8_t *tdi,
                                  uint8_t *tdo) {
    uint8_t tms_bits[JTAG_MAX_SEQUENCE_BYTES];
    memset(tms_bits, tms ? 0xff : 0, sizeof(tms_bits));
    jtag_run_cycles(count, tms_bits, tdi, tdo);
}

void JTAG_Sequence(uint32_t info, const uint8_t *tdi, uint8_t *tdo) {
    uint32_t count = info & JTAG_SEQUENCE_TCK;
    if (count == 0) {
        count = JTAG_MAX_SEQUENCE_BITS;
    }
    jtag_run_constant_tms(count, (info & JTAG_SEQUENCE_TMS) != 0,
                          tdi, (info & JTAG_SEQUENCE_TDO) ? tdo : NULL);
}

void SWJ_Sequence(uint32_t count, const uint8_t *data) {
    uint8_t tdi[JTAG_MAX_SEQUENCE_BYTES];
    memset(tdi, 0xff, sizeof(tdi));
    while (count != 0) {
        const uint32_t chunk = count > JTAG_MAX_SEQUENCE_BITS ? JTAG_MAX_SEQUENCE_BITS : count;
        jtag_run_cycles(chunk, data, tdi, NULL);
        data += (chunk + 7u) / 8u;
        count -= chunk;
    }
}

void JTAG_IR(uint32_t instruction) {
    const uint32_t index = DAP_Data.jtag_dev.index;
    const uint32_t count = DAP_Data.jtag_dev.count;
    if (count == 0 || index >= count) {
        return;
    }

    uint8_t zeros[JTAG_MAX_SEQUENCE_BYTES] = {0};
    uint8_t ones[JTAG_MAX_SEQUENCE_BYTES];
    memset(ones, 0xff, sizeof(ones));
    jtag_run_constant_tms(5, true, zeros, NULL);
    jtag_run_constant_tms(1, false, zeros, NULL);
    jtag_run_constant_tms(1, true, zeros, NULL);
    jtag_run_constant_tms(1, true, zeros, NULL);
    jtag_run_constant_tms(1, false, zeros, NULL);
    jtag_run_constant_tms(1, false, zeros, NULL);

    const uint32_t ir_before = DAP_Data.jtag_dev.ir_before[index];
    const uint32_t ir_after = DAP_Data.jtag_dev.ir_after[index];
    const uint32_t selected_ir_length = DAP_Data.jtag_dev.ir_length[index];
    const uint32_t total_bits = ir_before + selected_ir_length + ir_after;
    for (uint32_t base = 0; base < total_bits;) {
        const uint32_t chunk = total_bits - base > JTAG_MAX_SEQUENCE_BITS ?
            JTAG_MAX_SEQUENCE_BITS : total_bits - base;
        uint8_t tms_bits[JTAG_MAX_SEQUENCE_BYTES] = {0};
        uint8_t tdi_bits[JTAG_MAX_SEQUENCE_BYTES] = {0};
        for (uint32_t bit = 0; bit < chunk; ++bit) {
            const uint32_t position = base + bit;
            bool tdi = true;
            if (position >= ir_before && position < ir_before + selected_ir_length) {
                tdi = ((instruction >> (position - ir_before)) & 1u) != 0;
            }
            if (tdi) {
                tdi_bits[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
            }
            if (position == total_bits - 1u) {
                tms_bits[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
            }
        }
        jtag_run_cycles(chunk, tms_bits, tdi_bits, NULL);
        base += chunk;
    }
    jtag_run_constant_tms(1, true, zeros, NULL);
    jtag_run_constant_tms(1, false, ones, NULL);
}

uint32_t JTAG_ReadIDCode(void) {
    const uint32_t index = DAP_Data.jtag_dev.index;
    const uint32_t after = DAP_Data.jtag_dev.count - index - 1u;
    uint8_t zeros[JTAG_MAX_SEQUENCE_BYTES] = {0};
    uint8_t ones[JTAG_MAX_SEQUENCE_BYTES];
    uint8_t idcode_bytes[4] = {0};
    memset(ones, 0xff, sizeof(ones));

    jtag_run_constant_tms(1, true, zeros, NULL);
    jtag_run_constant_tms(1, false, zeros, NULL);
    jtag_run_constant_tms(1, false, zeros, NULL);
    if (index != 0) {
        jtag_run_constant_tms(index, false, ones, NULL);
    }

    if (after == 0) {
        uint8_t tms_bits[JTAG_MAX_SEQUENCE_BYTES] = {0};
        tms_bits[3] = 0x80;
        jtag_run_cycles(32, tms_bits, zeros, idcode_bytes);
    } else {
        jtag_run_constant_tms(32, false, zeros, idcode_bytes);
    }
    if (after != 0) {
        uint32_t remaining = after;
        while (remaining != 0) {
            const uint32_t chunk = remaining > JTAG_MAX_SEQUENCE_BITS ? JTAG_MAX_SEQUENCE_BITS : remaining;
            uint8_t tms_bits[JTAG_MAX_SEQUENCE_BYTES] = {0};
            if (remaining == chunk) {
                tms_bits[(chunk - 1u) / 8u] |= (uint8_t)(1u << ((chunk - 1u) % 8u));
            }
            jtag_run_cycles(chunk, tms_bits, ones, NULL);
            remaining -= chunk;
        }
    }
    if (after != 0) {
        jtag_run_constant_tms(1, true, zeros, NULL);
    }
    jtag_run_constant_tms(1, false, zeros, NULL);

    return (uint32_t)idcode_bytes[0] |
        ((uint32_t)idcode_bytes[1] << 8) |
        ((uint32_t)idcode_bytes[2] << 16) |
        ((uint32_t)idcode_bytes[3] << 24);
}

uint8_t JTAG_Transfer(uint32_t request, uint32_t *data) {
    (void)request;
    (void)data;
    return DAP_TRANSFER_ERROR;
}

void JTAG_WriteAbort(uint32_t data) {
    (void)data;
}

uint32_t dap_jtag_scan_idcode(void) {
    uint8_t tms_bits[JTAG_SCAN_CYCLE_COUNT / 8u] = {0};
    uint8_t tdi_bits[JTAG_SCAN_CYCLE_COUNT / 8u] = {0};
    uint8_t tdo_bits[JTAG_SCAN_CYCLE_COUNT / 8u] = {0};

    for (uint32_t bit = 0; bit < 6; ++bit) {
        tms_bits[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    }
    tms_bits[7u / 8u] |= (uint8_t)(1u << (7u % 8u));
    for (uint32_t bit = JTAG_SCAN_DATA_OFFSET + 31u;
         bit <= JTAG_SCAN_DATA_OFFSET + 32u; ++bit) {
        tms_bits[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    }

    jtag_run_cycles(JTAG_SCAN_CYCLE_COUNT, tms_bits, tdi_bits, tdo_bits);
    uint32_t idcode = 0;
    for (uint32_t bit = 0; bit < 32; ++bit) {
        const uint32_t sample = JTAG_SCAN_DATA_OFFSET + bit;
        if ((tdo_bits[sample / 8u] & (1u << (sample % 8u))) != 0) {
            idcode |= 1u << bit;
        }
    }
    return idcode;
}
