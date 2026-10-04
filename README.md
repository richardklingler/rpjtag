# rpjtag

This workspace contains the initial implementation scaffold for the RP2354B JTAG adapter described in `rpjtag-firmware.md`.

## Structure

- `fw/`: Pico 2 W firmware project
- `fw/test/`: host-side unit tests for TAP-state logic
- `tools/`: Python helper library for the native rpjtag protocol
- `docs/`: protocol notes and implementation references

## Build firmware

```bash
cd /Users/klingler/Nextcloud/Develop/Pico/rpjtag/fw
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

## Run tests

```bash
cd /Users/klingler/Nextcloud/Develop/Pico/rpjtag
python3 fw/test/test_tap.py
```

## Hardware target

- Board: Raspberry Pi Pico 2 W
- MCU: RP2354B
- Flash: built-in QSPI flash
- USB VID/PID: `0x1209` / `0x5306` (pid.codes registration)

The current scaffold covers the milestone M0 bring-up path: board init, LED/buttion checks, and ADC measurement of the target reference voltage (`VTref`).
