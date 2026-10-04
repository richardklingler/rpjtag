# Pico 2 W Development-Board Wiring

This provisional map is for bring-up on the Raspberry Pi Pico 2 W development board. It does not replace the custom RP2354B adapter pin proposal in `rpjtag-firmware.md`.

The Pico 2 W uses the RP2350A variant. Its external ADC inputs are GPIO26-GPIO29; GPIO40-GPIO47 are only available as ADC inputs on the RP2350B package. The onboard LED is connected through the CYW43 wireless chip, so this map uses external LEDs on header GPIOs instead.

Header pin numbers below refer to the standard 40-pin Pico header, viewed from above with USB at the top.

| Signal | GPIO | Header pin | Notes |
|---|---:|---:|---|
| UART TX | GP0 | 1 | Optional target UART |
| UART RX | GP1 | 2 | Optional target UART |
| JTAG TCK | GP2 | 4 | Connect through target-side buffer |
| JTAG TMS | GP3 | 5 | Connect through target-side buffer |
| JTAG TDI | GP4 | 6 | Connect through target-side buffer |
| JTAG TDO | GP5 | 7 | Connect through target-side buffer |
| JTAG buffer OE | GP6 | 9 | Keep buffers disabled until VTref is valid |
| JTAG buffer DIR | GP7 | 10 | Direction control |
| nSRST drive | GP8 | 11 | Open-drain driver control |
| nSRST sense | GP9 | 12 | Target reset sense input |
| nTRST drive | GP10 | 14 | Open-drain driver control |
| SPI SCK | GP12 | 16 | Optional direct SPI |
| SPI MOSI | GP13 | 17 | Optional direct SPI |
| SPI MISO | GP14 | 19 | Optional direct SPI |
| SPI CS | GP15 | 20 | Optional direct SPI |
| SPI buffer OE | GP16 | 21 | Optional direct SPI |
| Status LED | GP18 | 24 | External LED with series resistor |
| Activity LED | GP19 | 25 | External LED with series resistor |
| Target power enable | GP20 | 27 | Control input to a load switch only |
| User button | GP21 | 28 | Wire button to GND; firmware enables pull-up |
| VTref ADC | GP26 / ADC0 | 31 | Through the VTref divider |
| Target current ADC | GP27 / ADC1 | 32 | Optional current-sense output |

## Electrical Notes

- Connect Pico GND to target GND.
- Keep every ADC input between GND and 3.3 V. The current firmware assumes the VTref divider halves the target voltage and multiplies the ADC reading by two.
- Do not connect target JTAG or SPI signals directly to Pico GPIO. Use voltage-compatible buffers, series resistors, and the output-enable safety circuit from the hardware specification.
- GP20 is only a logic control for an external load switch; do not power a target from a Pico GPIO.
- The firmware initializes GP18, GP19, GP21, and GP26. The PIO/DMA JTAG engine uses GP2-GP5; reset, SPI, UART, and power-control assignments remain reserved for later milestones.
- GPIO23-GPIO25 and GPIO29 are used by the Pico 2 W wireless/power circuitry; leave them out of this development-board wiring.
- For the Seeed Spartan Edge Accelerator Board, use its dedicated FPGA JTAG connector and set K5 to JTAG mode. Verify connector orientation against the board schematic before wiring; do not use the 5 V Arduino shield I/O for direct Pico connections.
- M2 bench test: OpenOCD 0.12.0 returned IDCODE `0x03620093` at 1, 6, and 15 MHz. A local CMSIS-DAP-v2-enabled openFPGALoader build detects the XC7S15 at 1 MHz but fails at higher settings; SRAM bridge loading stalls before completion.