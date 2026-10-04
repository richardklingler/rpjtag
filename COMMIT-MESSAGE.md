# Commit Message

Add CMSIS-DAP v2 JTAG interface

- Add a CMSIS-DAP v2 vendor bulk interface alongside the CDC diagnostic interface.
- Integrate ARM's CMSIS-DAP command processor with JTAG-only configuration and PIO/DMA JTAG hooks.
- Use the low 16-bit response length returned by `DAP_ExecuteCommand` when sending USB replies.
- Default CMSIS-DAP TCK to 1 MHz for reliable first contact; clients can request faster rates.
- Add Microsoft OS 2.0 WinUSB descriptors, OpenOCD chain-scan instructions, and attribution for the pinned CMSIS-DAP source.

## Verification

- Pico 2 W firmware builds successfully with the configured Pico SDK, ARM toolchain, and pinned CMSIS-DAP source.
- `python3 fw/test/test_host_protocol.py` passes (5 tests).
- `python3 fw/test/test_tap.py` passes.
- OpenOCD 0.12.0 at 1 MHz found the CMSIS-DAP v2 interface and scanned the chain, reading Spartan-7 IDCODE `0x03620093`.
- A CMSIS-DAP-v2-enabled openFPGALoader build detected the XC7S15 at 1 MHz with explicit VID/PID.
- CDC `info` returned firmware 1.0, hardware revision 1, VTref 975 mV, and button state 0.
- PIO/DMA scans returned IDCODE `0x03620093` at requested TCK settings of 1, 6, and 15 MHz.
- The local v2-enabled openFPGALoader build returned invalid IDs at 6 and 15 MHz; the 1 MHz default works. Retest higher speeds after future timing changes.
- Windows WinUSB binding has not been tested on Windows.
- Homebrew openFPGALoader 1.1.1 lacks CMSIS-DAP; the local v2-enabled build used a macOS-only workaround in ignored test sources for libusb DMA allocation.
- Tried the bundled `spiOverJtag_xc7s15ftgb196.bit.gz` prebuilt image with `--write-sram`; transfer stalled at 23% and was interrupted. No flash-write option was used.
- TCK output has not been independently measured with a logic analyzer.

## Maintenance

Keep this file current as project changes are made. Update the title, change summary, and verification results to describe the latest pending changes; replace stale details instead of accumulating unrelated history.
