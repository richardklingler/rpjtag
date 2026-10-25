# Commit Message

Faster BSR streaming: DR-only captures, counted change stream, 2 KB FIFO

For GateLab's live pin view (its native JTAG engine streams this BSR).

- Stream captures after the first shift the DR only: SAMPLE stays in the
  IR, and the RTI -> Shift-DR -> RTI sequence is built once per stream
  and shifts only up to the farthest masked cell (leaving Shift-DR early
  is harmless under SAMPLE/PRELOAD). Any other TAP/chain/BSR command
  invalidates it, and the next capture takes the full IR + DR path.
- Stream mode 2: change-only with a u32 captures-since-start field after
  the drop count (16-byte header) and a frame without changes at least
  every 100 ms, so a host knows the capture rate and how long each level
  lasted. Modes 0 and 1 are unchanged.
- Vendor TX FIFO 256 -> 2048 bytes: GateLab saw 97 dropped captures at
  2,372 captures/s with the 256-byte FIFO (four XC7S15 frames); streaming
  is no longer limited to BSRs of at most 1888 bits.
- GET_INFO: firmware 0x0101, capability bit 5 (0x3f) for the above.
- Host client: mode 2 decoding (`BsrStreamFrame.captures`); docs.

## Verification

- `python3 fw/test/test_native_protocol.py` (11, new: counted change
  stream), `fw/test/test_host_protocol.py`, `fw/test/test_tap.py` pass.
- Built on the MacBook Air (Arm GNU Toolchain 15.3.rel2, SDK 2.3.1,
  build folder /tmp/rpjtag-build): no warnings from the firmware.
- On the Spartan Edge (XC7S15, blinky) through GateLab at 4 MHz, mode 2
  masked to led[0]/led[1]: 3,124 captures/s (full vectors: 2,375),
  0 dropped in 30 s, a heartbeat every 100 ms, led[0] edges
  0.6709-0.6714 s apart (2^26 / 100 MHz = 0.67109 s).
- openocd's SRAM load through the CMSIS-DAP v2 interface went from
  17.1 to 13.9 s, likely the bigger vendor TX buffer, which that
  interface shares.

## Build on either Mac

- fw/CMakeLists.txt no longer hard-codes ~/Nextcloud/...: it takes
  -DPICO_SDK_PATH or the environment's, else the pico-sdk next to the
  repo (../../pico-sdk), which is where it sits on both Macs.
