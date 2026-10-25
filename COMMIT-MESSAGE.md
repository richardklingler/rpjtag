# Commit Message

Native SRAM loads: raw scans and no idle sleep while data arrives

For GateLab's native Xilinx 7-series SRAM loads over interface 1.

- The main loop no longer sleeps 1 ms while native bytes arrived in
  the last 100 ms (native_protocol_is_busy). TinyUSB takes one 64-byte
  packet per tud_task(), so the sleep capped native intake at ~64 KB/s:
  an XC7S15 bitstream (538,844 bytes) took 11.3-11.9 s whatever the
  TCK. Now 3.7 s at 4 MHz, 3.1 s at 9.375 MHz, 2.9 s at 15 MHz.
- SCAN_IR/SCAN_DR flag bit 1: raw, no BYPASS padding for the other
  devices of a configured chain. A bitstream split across DR scans
  (ending in Pause-DR) gets one padding bit per scan otherwise, which
  corrupts it on a multi-device chain (the Cyclone IV + XC7S15 bench).
  The host pads once at the start and end itself.
- GET_INFO: firmware 0x0102, capability bit 6 (0x7f).
- Host client: scan_dr(raw=True); docs/protocol.md.

## Verification

- Host tests pass (test_native_protocol 12, new raw-flag test;
  test_host_protocol; test_tap).
- Built on the MacBook Air, flashed by the user; GateLab's opt-in load
  spike on the Spartan Edge (XC7S15 alone) loaded button-led and blinky
  six times with raw scans: DONE high and the LEDs right at 4, 9.375
  and 15 MHz; at 25 MHz DONE stayed low (too fast for the wiring).
