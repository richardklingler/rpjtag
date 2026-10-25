# Commit Message

Faster native intake (0x0104); the 0x0103 load times

- Vendor RX FIFO 256 -> 4096 bytes: several native frames wait while
  the PIO shifts a bitstream piece.
- native_protocol_task: when the FIFO runs empty it calls tud_task()
  and reads again instead of going round the main loop (TinyUSB moves
  one 64-byte packet per tud_task()).
- dap_jtag_write_stream: tud_task() while DMA feeds the PIO, so USB
  keeps receiving during the shift.
- GET_INFO: firmware 0x0104 (capabilities unchanged, 0xff).
- docs/protocol.md: the 0x0103 times measured on the user's Spartan
  Edge (XC7S15, 538,844 bytes): 1.93 s at 4 MHz, 1.31 s at 9.375 MHz
  (0x0102: 3.7 s, 3.1 s); GateLab's Build & Program 2.3 s at 4 MHz.
  25 MHz still fails on the J8 wiring.

## Verification

- Host tests pass; built on the MacBook Air with no firmware warnings.
- To measure on the Spartan Edge after flashing: GateLab's load spike.
