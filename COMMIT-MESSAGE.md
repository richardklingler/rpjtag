# Commit Message

Add native BSR sampling and streaming

- Implement BSR_STREAM_START/STOP with full-vector and change-only modes, optional masks, timestamps, and dropped-capture accounting.
- Add host frame parsing/queueing and tests for unsolicited stream frames and deltas.
- Combine BSR TAP navigation, SAMPLE IR, and capture DR into one bounded JTAG sequence to reduce PIO/DMA setup overhead.
- Add read-only BSR_CONFIG and BSR_SAMPLE commands using per-device BSDL metadata; do not select EXTEST.
- Add PyUSB BSR helpers/tests and document the XC7S15 FTGB196's 339-bit BSR and instruction opcodes.
- Add a second vendor bulk interface for the native protocol while preserving CMSIS-DAP on interface 0 and moving CDC to interfaces 2/3.
- Implement bounded little-endian framing, USB-stream reassembly, GET_INFO, SET_TCK, GET_VTREF, TAP reset/goto, IR/DR scans, scan checks, RUNTEST, BATCH, and chain detect/configuration.
- Add arbitrary-length bit-packed PIO/DMA sequences, split into safe 64-cycle chunks, and automatic BYPASS padding for configured chains.
- Add a PyUSB client, framing/command tests, payload documentation, native-interface usage examples, and CDC counters for native receive/frame/response diagnosis.

## Verification

- Pico 2 W firmware builds successfully with the configured Pico SDK, ARM toolchain, and pinned CMSIS-DAP source.
- `python3 fw/test/test_tap.py`, `python3 fw/test/test_host_protocol.py`, and `python3 fw/test/test_native_protocol.py` pass (10 native protocol tests).
- `git diff --check` passes and workspace diagnostics report no errors in the changed C/Python sources.
- Live BSR_SAMPLE was validated on the XC7S15: BSR_CONFIG with SAMPLE/PRELOAD `0x01`, EXTEST `0x26`, and BYPASS `0x3f` returned 43 captured bytes.
- The blinky on IO_L4N_T0_D05_14 (ball A13, BSR input cell 308) toggled every approximately 0.5 seconds during 1,616 read-only captures in 3.001 seconds (about 538 captures/s).
- Live change-only streaming reported cell 308 transitions at approximately 0.5-second intervals with zero drops. A 60-second masked run delivered 122 frames including 121 transitions with zero drops.
- Before the combined-scan optimization, full-vector mode delivered 3,437 frames in 3.000 seconds (1,145.5 frames/s) at 1 MHz with one dropped capture; at an applied 9.375 MHz it delivered 5,515 frames in 3.001 seconds (1,838 frames/s) with 10 drops.
- The combined-scan firmware builds and all host tests pass. Its live capture rate and the one-hour no-loss acceptance run await reflashing the newest UF2.
- The reflashed diagnostic image enumerates native USB correctly; GET_INFO succeeds with `rx_bytes=8`, `frames=1`, and `responses=1`. CDC and CMSIS-DAP remain functional.
- Correcting the TAP reset transition yields native DR bytes `93006203`; CHAIN_DETECT reports the XC7S15 IDCODE `0x03620093` and aggregate IR length 6.
- CHAIN_CONFIG succeeds for the detected single-device chain, configured DR capture still returns `93006203`, and a TAP_RESET+RUNTEST BATCH returns success for both commands.
- With the Cyclone IV and Spartan-7 connected, CHAIN_DETECT reports IDs `0x020F20DD` and `0x03620093` with 16 aggregate IR bits. Selecting the Spartan-7 IDCODE instruction automatically places the Cyclone IV in BYPASS; the active-device DR scan returns `0x03620093`.
- OpenOCD continues to scan the Spartan-7 IDCODE `0x03620093`.
- Existing M2 hardware results remain valid: OpenOCD scanned Spartan-7 IDCODE `0x03620093` at 1/6/15 MHz, and the supplied XC7S15 bitstream loaded into SRAM at those rates. No flash write was performed.
- Windows WinUSB binding and independent logic-analyzer TCK measurement remain unverified.
