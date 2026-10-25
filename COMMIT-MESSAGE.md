# Commit Message

Fast write-only DR shifts through a second PIO program

For GateLab's native Xilinx 7-series SRAM loads: the CPU work around
each 4,096-bit piece (bit-by-bit copying and packing, a PIO restart
and DMA set-up per 512 bits, one RX word per bit even without capture)
cost about 2.5-2.8 ms per piece, so a 538,844-byte XC7S15 bitstream
took 4.0 s at 4 MHz and 2.9 s at 15 MHz.

- fw/src/jtag_write.pio: TDI only, TCK on side-set, two PIO cycles per
  TCK, autopull of 32-bit words (bit 0 of byte 0 first); TMS keeps its
  level (low in Shift-DR); the out stalls with TCK low when the FIFO
  is empty.
- dap_jtag_write_stream(): copies whole words to an aligned buffer,
  DMAs them into the TX FIFO, waits for the FIFO to empty and the
  program to stall, then switches back to the transfer program.
- scan_register(): a DR scan without TDO capture and without padding
  (raw, or a one-device chain), longer than 64 bits, streams its whole
  words that way; the rest and the last bit (TMS high) take the usual
  path. Every other scan is unchanged.
- GET_INFO: firmware 0x0103, capability bit 7 (0xff); docs.

## Verification

- Host tests pass; built on the MacBook Air with no firmware warnings.
- To measure on the Spartan Edge after flashing: GateLab's opt-in load
  spike (DONE and the LEDs after each load, times at 4, 9.375, 15 MHz).
