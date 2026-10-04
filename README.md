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
python3 fw/test/test_host_protocol.py
```

## Query the attached board

Install the host serial dependency and query the adapter over its USB CDC port:

```bash
python3 -m pip install -r requirements.txt
python3 tools/rpjtag.py info --port /dev/cu.usbmodemXXXX
python3 tools/rpjtag.py vtref --port /dev/cu.usbmodemXXXX
python3 tools/rpjtag.py tck --hz 1000000 --port /dev/cu.usbmodemXXXX
python3 tools/rpjtag.py idcode --port /dev/cu.usbmodemXXXX
```

Replace `/dev/cu.usbmodemXXXX` with the serial device name shown on the host.
The IDCODE scan uses PIO and DMA. Set TCK to 1, 6, or 15 MHz with `--hz` before scanning to check the requested rates.

## Hardware target

- Board: Raspberry Pi Pico 2 W
- Development-board MCU: RP2350A
- Intended custom-adapter MCU: RP2354B, as described in `rpjtag-firmware.md`
- Flash: built-in QSPI flash
- USB VID/PID: `0x1209` / `0x5306` (pid.codes registration)

The current scaffold covers the milestone M0 bring-up path: board init, LED/button checks, and ADC measurement of the target reference voltage (`VTref`). See [docs/pico2w-wiring.md](docs/pico2w-wiring.md) for the provisional Pico 2 W header map. It is separate from the eventual custom-adapter pin assignment in the firmware specification.
