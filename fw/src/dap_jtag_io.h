#ifndef RPJTAG_DAP_JTAG_IO_H
#define RPJTAG_DAP_JTAG_IO_H

#include <stdbool.h>
#include <stdint.h>

void dap_jtag_init(void);
void dap_jtag_pio_enable(void);
void dap_jtag_pio_disable(void);
bool dap_jtag_set_tck(uint32_t frequency_hz);
uint32_t dap_jtag_get_tck(void);
uint32_t dap_jtag_pin_input(unsigned int pin_index);
void dap_jtag_pin_output(unsigned int pin_index, uint32_t value);
void dap_jtag_sequence_bits(uint32_t bit_count, const uint8_t *tms_bits,
						   const uint8_t *tdi_bits, uint8_t *tdo_bits);
uint32_t dap_jtag_scan_idcode(void);
/* Shifts `bits` (a multiple of 32, at most 4096) write-only through the write program: TDI from `tdi` (bit 0 of
 * byte 0 first), TMS held at its last level. The TAP must be in Shift-DR/IR with TMS low; it stays there. */
void dap_jtag_write_stream(const uint8_t *tdi, uint32_t bits);

#endif