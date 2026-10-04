# Handover

**Updated:** 2026-10-04
**Repository:** clean `main`, synced with `origin/main` at `551707a`

## Project State

M0-M3 are implemented and hardware-validated. M4 has read-only boundary-register
configuration/sample and both streaming modes. The full one-hour change-only soak
passed on the attached two-device JTAG chain. M4's capture-rate acceptance criterion
remains open: compare measured rate with a well-defined calculated rate before
declaring M4 complete.

The repository was clean and synced before this handover was created. `HANDOVER.md`
is currently untracked; the latest tested firmware is
`fw/build/rpjtag_firmware.uf2` (build artifacts are local and ignored).

## Hardware Setup

- Adapter: Raspberry Pi Pico 2 W / RP2350A, firmware uses VID:PID `1209:5306`.
- JTAG chain: Cyclone IV followed by Spartan Edge Accelerator XC7S15-1FTGB196C.
- `CHAIN_DETECT` order (nearest TDO first): Cyclone IV `0x020F20DD`, Spartan-7 `0x03620093`; aggregate IR length 16.
- Chain configuration used: IR lengths `[10, 6]`, BYPASS opcodes `[0x3ff, 0x3f]`, active Spartan-7 index 1.
- Current requested TCK was restored to 1 MHz after the faster-clock test.
- Spartan blinky LED L2 is routed to `IO_L4N_T0_D05_14`, package ball A13. The BSDL
  calls this port `IO_A13`; boundary cell 307 is output/control and cell 308 is
  input.
- `hw/bsdl/xc7s15_ftgb196.bsd` is marked preliminary/unverified. Treat other cell mappings as provisional.

## Implementation Pointers

- `fw/src/native_protocol.c`: native framing/dispatch, chain/TAP logic, BSR
  config/sample/stream, combined chain-aware SAMPLE IR + capture DR path.
- `fw/src/dap_jtag_io.c`: PIO/DMA sequence backend. Internal bit sequences batch up
  to 512 clocks; CMSIS-DAP `JTAG_Sequence` remains capped at 64 clocks.
- `tools/rpjtag_native.py`: PyUSB client, BSR setup/sample, stream start/stop, and full/delta event parsing.
- `fw/test/test_native_protocol.py`: native framing, BSR payload, and unsolicited stream-event tests.
- `docs/protocol.md`: wire format and BSDL notes.
- `COMMIT-MESSAGE.md`: current change summary and bench results.

BSR is read-only: the exercised path selects SAMPLE/PRELOAD `0x01`; it does not
select EXTEST. The configured XC7S15 values are BSR length 339, EXTEST `0x26`,
BYPASS `0x3f`, and a safe vector of 43 zero bytes.

## Verified Results

- Firmware builds with the configured Pico SDK and Arm GNU toolchain.
- Host tests pass: `fw/test/test_tap.py` (5), `fw/test/test_host_protocol.py` (5),
  and `fw/test/test_native_protocol.py` (10).
- M3 two-device scan: both IDCODEs detected; selecting Spartan IDCODE `0x09`
  automatically puts the Cyclone IV in BYPASS; Spartan DR returns `0x03620093`.
- BSR_SAMPLE returns 43 bytes. Cell 308 toggled about every 0.5 seconds, matching the 1-second L2 blink period.
- One-minute masked change-only run: 122 frames, 121 transitions, zero drops.
- One-hour masked change-only run at 1 MHz and 10 ms requested interval: 3,600.1
  seconds, 7,202 frames, 7,201 transitions, one baseline resync, zero drops.
- Latest 512-clock-batch image, full-vector at 1 MHz: 4,256 frames in 3.000
  seconds, 1,418.5 captures/s, six cell-308 transitions, zero drops.
- At applied 9.375 MHz: 7,926 frames in 3.000 seconds, 2,641.9 captures/s, 32
  drops. TCK was restored to 1 MHz afterward.
- `git diff --check` passed before this file was created.

## Remaining M4 Work

The milestone requires measured capture rate within 20% of a calculated rate. A
cycle-only estimate for approximately 367 TCKs at 1 MHz is about 2,725 captures/s,
notably above the observed 1,418.5/s. Do not treat that simple estimate as
definitive: clarify whether the expected calculation includes TAP navigation,
PIO/DMA setup, native USB framing, and host consumption. No logic-analyzer TCK
measurement has been made.

Next, instrument or measure capture-cycle time separately from USB/frame handling,
derive the expected M4 rate from that model, and either optimize the measured
bottleneck or document why the original estimate omitted it. Then add
`docs/milestones/M4.md` with the tested setup and acceptance results. The one-hour
change-only no-loss criterion is already satisfied.

## Useful Commands

Build and run host tests:

```bash
cd fw
export PATH="/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin:$PATH"
cmake --build build -j4
cd ..
/usr/bin/python3 fw/test/test_tap.py
/usr/bin/python3 fw/test/test_host_protocol.py
/usr/bin/python3 fw/test/test_native_protocol.py
```

The native Python client can configure/sample or stream the BSR. For the blinky pin,
use mask bit 308: byte index 38, bit mask `0x10`. Keep the active device at index 1
and chain metadata `[10, 6]` / `[0x3ff, 0x3f]`.
