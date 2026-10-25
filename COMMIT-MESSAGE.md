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
- Flashed by the user; GateLab's load spike on the Spartan Edge (DONE
  and the LEDs right on every load): 1.27 s at 4 MHz, 0.92-0.95 s at
  9.375 MHz, 0.99 s at 15 MHz, 0.95 s at 25 MHz (which loaded this
  time). From 9.375 MHz on the time no longer depends on TCK: about
  580 KB/s, USB full-speed. GateLab's Build & Program of blinky at
  4 MHz: 1.6 s, at 10 MHz: 1.2 s (0x0103: 2.3 s, openocd: 13.9 s).
