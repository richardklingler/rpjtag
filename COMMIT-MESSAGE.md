# Commit Message

Add rpJTAG Pico 2 W firmware bring-up scaffold

- Add the initial Pico 2 W / RP2354B firmware project with GPIO, button, LED, and VTref ADC bring-up.
- Enable USB CDC stdio and set the device identity to VID/PID `0x1209:0x5306`, manufacturer `Klingler Engineering`, and product `rpJTAG`.
- Add native-protocol notes, a Python host-side helper, and TAP-state tests.
- Document build and test commands, update the firmware specification with the registered USB IDs, and ignore `fw/build/` output.

## Scope

This is an M0 bring-up scaffold. USB currently provides CDC stdio for enumeration; CMSIS-DAP and the native USB interface described in the specification are not implemented yet.

## Verification

- `cmake --build fw/build -j4` succeeds with the configured Pico SDK and ARM toolchain.
- `python3 fw/test/test_tap.py` passes.

## Maintenance

Keep this file current as project changes are made. Update the title, change summary, and verification results to describe the latest pending changes; replace stale details instead of accumulating unrelated history.
