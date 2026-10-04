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
The IDCODE scan uses PIO and DMA. The attached Spartan-7 returned `0x03620093` at requested TCK settings of 1, 6, and 15 MHz.

## CMSIS-DAP v2

The firmware exposes a CMSIS-DAP v2 bulk interface as USB interface 0 and keeps the CDC diagnostics as a separate interface. SWD is not enabled; JTAG operations use the PIO/DMA engine. The CMSIS-DAP default clock is 1 MHz for reliable first contact; OpenOCD can request faster clocks with `adapter speed`.

After flashing the UF2, scan the connected Spartan-7 with OpenOCD:

```bash
openocd -f interface/cmsis-dap.cfg \
	-c "transport select jtag" \
	-c "adapter speed 1000" \
	-c "jtag newtap fpga tap -irlen 6" \
	-c "init; scan_chain; shutdown"
```

OpenOCD 0.12.0 successfully connected through this interface and scanned the chain, finding the Spartan-7 IDCODE `0x03620093`. A CMSIS-DAP-v2-enabled openFPGALoader build also detects the XC7S15 at 1 MHz.

For openFPGALoader, use a CMSIS-DAP v2-enabled build and pass the registered IDs explicitly:

```bash
openFPGALoader -c cmsisdap --vid 0x1209 --pid 0x5306 --freq 1000000 --detect
```

The Homebrew 1.1.1 bottle on the test host has CMSIS-DAP disabled. A local CMSIS-DAP-v2-enabled build loaded the supplied XC7S15 bitstream into SRAM at 1, 6, and 15 MHz.

## Hardware target

- Board: Raspberry Pi Pico 2 W
- Development-board MCU: RP2350A
- Intended custom-adapter MCU: RP2354B, as described in `rpjtag-firmware.md`
- Flash: built-in QSPI flash
- USB VID/PID: `0x1209` / `0x5306` (pid.codes registration)

The current scaffold covers M0 bring-up and the M1 JTAG engine, with the M2 CMSIS-DAP v2 interface now integrated. See [docs/pico2w-wiring.md](docs/pico2w-wiring.md) for the provisional Pico 2 W header map. It is separate from the eventual custom-adapter pin assignment in the firmware specification.
