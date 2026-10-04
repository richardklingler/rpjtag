# Commit Message

Add native JTAG protocol interface

- Add a second vendor bulk interface for the native protocol while preserving CMSIS-DAP on interface 0 and moving CDC to interfaces 2/3.
- Implement bounded little-endian framing, USB-stream reassembly, GET_INFO, SET_TCK, GET_VTREF, TAP reset/goto, IR/DR scans, scan checks, RUNTEST, BATCH, and chain detect/configuration.
- Add arbitrary-length bit-packed PIO/DMA sequences, split into safe 64-cycle chunks, and automatic BYPASS padding for configured chains.
- Add a PyUSB client, framing/command tests, payload documentation, native-interface usage examples, and CDC counters for native receive/frame/response diagnosis.

## Verification

- Pico 2 W firmware builds successfully with the configured Pico SDK, ARM toolchain, and pinned CMSIS-DAP source.
- `python3 fw/test/test_tap.py`, `python3 fw/test/test_host_protocol.py`, and `python3 fw/test/test_native_protocol.py` pass.
- `git diff --check` passes and workspace diagnostics report no errors in the changed C/Python sources.
- The reflashed diagnostic image enumerates native USB correctly; GET_INFO succeeds with `rx_bytes=8`, `frames=1`, and `responses=1`. CDC and CMSIS-DAP remain functional.
- Correcting the TAP reset transition yields native DR bytes `93006203`; CHAIN_DETECT reports the XC7S15 IDCODE `0x03620093` and aggregate IR length 6.
- CHAIN_CONFIG succeeds for the detected single-device chain, configured DR capture still returns `93006203`, and a TAP_RESET+RUNTEST BATCH returns success for both commands.
- OpenOCD continues to scan IDCODE `0x03620093`. The required two-device chain/BYPASS-padding hardware test remains outstanding because only one JTAG device is attached.
- Existing M2 hardware results remain valid: OpenOCD scanned Spartan-7 IDCODE `0x03620093` at 1/6/15 MHz, and the supplied XC7S15 bitstream loaded into SRAM at those rates. No flash write was performed.
- Windows WinUSB binding and independent logic-analyzer TCK measurement remain unverified.
