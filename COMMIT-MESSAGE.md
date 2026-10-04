# Commit Message

Add M0 USB CDC query path

- Add newline-delimited `INFO` and `VTREF` commands to the Pico 2 W firmware.
- Replace the host helper's hard-coded info response with a PySerial-backed CDC client and `info`/`vtref` CLI commands.
- Add fake-serial tests, declare the PySerial dependency, and document the temporary M0 command interface.

## Scope

USB CDC still provides the transport; the binary native vendor-class interface and JTAG engine remain future milestones. A live INFO round-trip has been confirmed on the attached board; the reported VTref reading is only 85 mV and still needs electrical validation.

## Verification

- `python3 fw/test/test_host_protocol.py` passes.
- `python3 fw/test/test_tap.py` passes.
- Firmware builds successfully with the configured Pico SDK and ARM toolchain.
- Attached-board `info` query returned firmware version 256, hardware revision 1, VTref 85 mV, and button state 0.
- VTref accuracy has not been validated against a known voltage.

## Maintenance

Keep this file current as project changes are made. Update the title, change summary, and verification results to describe the latest pending changes; replace stale details instead of accumulating unrelated history.
