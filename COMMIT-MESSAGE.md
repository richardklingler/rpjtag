# Commit Message

Move JTAG IDCODE shifting to PIO and DMA

- Add a PIO JTAG clock/data loop with DMA-fed TMS/TDI and DMA-captured TDO for the 32-bit IDCODE scan.
- Add a configurable TCK divider and expose `tck --hz` through the USB CDC host CLI.
- Document the PIO/DMA path, supported development-board wiring, and TCK test commands.

## Verification

- Pico 2 W firmware builds successfully with the configured Pico SDK and ARM toolchain.
- `python3 fw/test/test_host_protocol.py` passes (5 tests).
- `python3 fw/test/test_tap.py` passes.
- No diagnostics reported for `fw/src/main.c`.
- PIO/DMA scans on the connected Spartan board returned IDCODE `0x03620093` at requested TCK settings of 1, 6, and 15 MHz.

## Maintenance

Keep this file current as project changes are made. Update the title, change summary, and verification results to describe the latest pending changes; replace stale details instead of accumulating unrelated history.
