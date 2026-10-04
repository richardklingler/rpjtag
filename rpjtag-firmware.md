# rpjtag — RP2354B JTAG / Boundary-Scan Adapter

Implementation spec for the adapter hardware and firmware.
Companion document: `jtaglab-macos.md` (macOS host app).

Working names: **rpjtag** (adapter), **JTAGLab** (macOS app). Both are placeholders.

---

## 1. Goals

1. A USB JTAG adapter based on the **RP2354B** (QFN-80, 48 GPIO, 2 MB stacked flash, 520 KB SRAM).
2. **Standard compatibility:** works as a CMSIS-DAP v2 probe with OpenOCD, openFPGALoader and pyOCD. No host driver needed on macOS, Linux or Windows.
3. **Intelligent offload:** the firmware runs boundary-scan bus cycles, flash algorithms and sample loops itself, so full-speed USB round-trips don't limit performance.
4. **Live pin sampling:** continuous SAMPLE/PRELOAD captures streamed to the host for the JTAGLab pin viewer.
5. **Electrically robust:** 1.2–3.6 V targets, VTref measurement, protected I/O.

### Non-goals (for now)

- USB high speed (the RP2354 has full speed only).
- 5 V targets (would need different buffers; possible later as an adapter board).
- IEEE 1149.7 (cJTAG) and 1149.6 AC-coupled test.
- Standalone/offline programming without a host (possible later; see section 9).

---

## 2. Hardware

### 2.1 Block overview

```
USB-C (FS) ── RP2354B ──┬── SN74AVC4T774 #1 ── TCK, TMS, TDI (out), TDO (in) ── target
                        ├── SN74AVC4T774 #2 ── SPI direct: SCK, MOSI (out), MISO (in), CS (out)
                        ├── open-drain FETs ── nSRST, nTRST
                        ├── ADC ── VTref divider
                        ├── optional load switch ── target power 3.3 V (≤ 150 mA)
                        └── UART level shifter (optional) ── target UART
```

- **Buffers:** SN74AVC4T774 (or 74LVC1T45 per signal). Each signal has a fixed direction set by DIR pins under firmware control. Do **not** use auto-direction shifters (TXS01xx), which misbehave with JTAG.
- **Voltage domains:** VCCA = 3.3 V (RP side), VCCB = VTref (target side). If VTref < 1.1 V, keep buffer outputs disabled (OE high).
- **Series resistors:** 33 Ω on all target outputs; ESD array (e.g. TPD4E05U06) on the connector side.
- **nSRST / nTRST:** open-drain via small N-MOSFETs. Also read back nSRST so the firmware can detect when the target holds it low.
- **VTref sense:** resistor divider into an ADC input. On the RP2354B the ADC channels are GPIO40–47.
- **Target power (optional):** load switch (e.g. TPS22918) providing 3.3 V, with current limit and a firmware enable.
- **UI:** one RGB or two single LEDs (status / activity), one button (BOOTSEL + user function).

### 2.2 Connectors

| Connector | Purpose |
|---|---|
| 2×5 1.27 mm (ARM Cortex Debug) | Primary JTAG (also SWD later) |
| 2×7 2.0 mm (Xilinx-style) | Via adapter board, so existing flying leads can be reused |
| 2×5 2.54 mm (Altera-style) | Via adapter board |
| 1×6 2.54 mm | SPI direct (VTref, GND, SCK, MOSI, MISO, CS) |
| 1×4 2.54 mm | UART (optional) |

### 2.3 Pin assignment (proposal; adjust to the layout)

| Function | GPIO | Notes |
|---|---|---|
| TCK | 2 | PIO side-set |
| TMS | 3 | PIO set pin or SIO |
| TDI | 4 | PIO out |
| TDO | 5 | PIO in |
| JTAG buffer OE / DIR | 6, 7 | |
| nSRST out / in | 8, 9 | |
| nTRST out | 10 | |
| SPI SCK / MOSI / MISO / CS | 12–15 | Second PIO SM |
| SPI buffer OE | 16 | |
| UART TX / RX | 0, 1 | UART0 |
| Target power enable | 20 | |
| LEDs | 24, 25 | |
| VTref ADC | 40 | ADC0 |
| Target current sense (optional) | 41 | ADC1 |

Keep the JTAG pins consecutive (TCK, TMS, TDI, TDO) so PIO pin mapping stays simple.

---

## 3. Firmware architecture

### 3.1 Stack

- C11, **Pico SDK 2.x**, CMake, **TinyUSB** (bundled with the SDK).
- No dynamic allocation after init. All buffers static.
- Core 0: USB, protocol parsing, command queue.
- Core 1: JTAG engine (PIO + DMA), BSR engine, flash algorithms, sample streaming.
- Inter-core communication: lock-free SPSC queues for command and response descriptors; payload in shared static buffers.

### 3.2 USB composite device

| Interface | Class | Purpose |
|---|---|---|
| 0 | Vendor (bulk IN/OUT), string contains **"CMSIS-DAP"** | CMSIS-DAP v2 |
| 1 | Vendor (bulk IN/OUT), string "rpjtag native" | Native offload protocol (section 5) |
| 2+3 | CDC-ACM | Target UART bridge (optional, milestone M6) |

- Include MS OS 2.0 descriptors (WinUSB) so Windows binds both vendor interfaces without a driver.
- VID/PID: use a pid.codes or Raspberry Pi community PID for development, and register a proper one before release.
- The interface string must contain "CMSIS-DAP"; that is how OpenOCD and others detect a CMSIS-DAP v2 interface.

### 3.3 JTAG engine (PIO)

- One PIO state machine shifts data: side-set drives **TCK**, `out` drives **TDI**, `in` samples **TDO**. Autopull/autopush at 32 bits, DMA on both FIFOs.
- **TMS:** set by the CPU via SIO between shift bursts, as debugprobe and DirtyJTAG do. A shift of N bits is executed as N−1 bits with TMS=0 by PIO, then the last bit with TMS=1 (the transition to Exit1), done either by a short PIO path or by bit-banging that one bit.
- **TAP state tracking** in software (16-state machine). Provide `tap_goto(state)` using the shortest TMS paths.
- **TCK:** set via the PIO clock divider. Range 10 kHz – 30 MHz (sys clock 150 MHz). Default 6 MHz.
- **TDO sampling point:** configurable (default: sample on rising TCK; optional delay compensation for long cables and buffer delay).
- **Write-only scans** (TDO not needed) skip the RX DMA. This matters for BSR bus cycles.
- **Chain handling:** firmware knows the chain (per-device IR length, position of the active device) and inserts BYPASS padding automatically, like SVF HIR/TIR/HDR/TDR.

### 3.4 CMSIS-DAP implementation

Base it on ARM's CMSIS-DAP reference implementation (Apache-2.0) or the Raspberry Pi debugprobe (MIT), with the JTAG I/O layer replaced by the PIO engine above.

Required commands for JTAG use by OpenOCD and openFPGALoader:

- `DAP_Info`, `DAP_HostStatus`, `DAP_Connect` (JTAG mode), `DAP_Disconnect`
- `DAP_SWJ_Clock`, `DAP_SWJ_Sequence`, `DAP_SWJ_Pins`
- `DAP_JTAG_Sequence` (the main workhorse), `DAP_JTAG_Configure`, `DAP_JTAG_IDCODE`
- `DAP_Delay`, `DAP_ResetTarget`
- Packet size 64 bytes (FS), packet count ≥ 4.

SWD support is a bonus (milestone M7) and turns the adapter into a general ARM debug probe.

---

## 4. Memory budget

| Item | Size |
|---|---|
| Firmware code | in 2 MB stacked flash (XIP); hot paths in RAM (`__not_in_flash_func`) |
| USB endpoint and protocol buffers | ~8 KB |
| BSR vectors: current, safe, capture, mask (max BSR 4096 bits = 512 B each) | ~4 KB |
| Pin map (cell index → role) for bus mode | ~4 KB |
| Data double buffer for flash programming | 2 × 32 KB |
| Readback / verify buffer | 32 KB |
| Sample stream ring buffer | 64 KB |
| Free | > 250 KB |

Streaming is the design rule: the host sends data blocks continuously, and the device never needs the whole image in RAM.

---

## 5. Native protocol ("rpjtag native", interface 1)

### 5.1 Framing

Little-endian. Each message, in both directions:

```
struct msg_hdr {
    uint8_t  cmd;      // command code
    uint8_t  seq;      // sequence number, echoed in the response
    uint16_t flags;    // bit0: more fragments, bit1: no response needed
    uint32_t len;      // payload length in bytes
};                     // followed by payload, split across 64-byte USB packets
```

Responses carry the same `cmd` and `seq` plus a status byte as the first payload byte (0 = OK, otherwise an error code, see 5.3).

Several commands can be queued without waiting (pipelining). The device processes them in order. The host matches responses by `seq`.

### 5.2 Commands

**System**

| Code | Name | Payload → Response |
|---|---|---|
| 0x01 | GET_INFO | → fw version, hw rev, serial, capabilities bitmap, max BSR length, buffer sizes |
| 0x02 | SET_TCK | u32 Hz → actual Hz |
| 0x03 | GET_VTREF | → u16 mV, u16 target current mA (if available) |
| 0x04 | IO_ENABLE | u8 on/off → refused if VTref out of range |
| 0x05 | TARGET_POWER | u8 on/off |
| 0x06 | RESET_LINES | u8 mask (nSRST, nTRST), u8 values, u32 hold µs |
| 0x0F | REBOOT | u8 mode (0 normal, 1 BOOTSEL) |

**TAP primitives**

| Code | Name | Payload |
|---|---|---|
| 0x10 | TAP_RESET | — (5× TMS=1, or nTRST if present) |
| 0x11 | TAP_GOTO | u8 target state |
| 0x12 | SCAN_IR | u16 bits, u8 end state, u8 flags (capture), TDI bytes → TDO bytes |
| 0x13 | SCAN_DR | u32 bits, u8 end state, u8 flags, TDI bytes → TDO bytes |
| 0x14 | SCAN_DR_CHECK | u32 bits, TDI, expected, mask → status + first mismatch bit |
| 0x15 | RUNTEST | u32 TCK cycles, u32 min µs, u8 end state |
| 0x16 | BATCH | sequence of 0x10–0x15 sub-commands, one response at the end |

**Chain**

| Code | Name | Payload |
|---|---|---|
| 0x20 | CHAIN_DETECT | → device count, IDCODEs (or 0 for BYPASS-only devices), detected total IR length |
| 0x21 | CHAIN_CONFIG | per device: IR length, BYPASS opcode; active device index |

Once configured, all following commands address only the active device and padding is automatic.

**Boundary scan**

| Code | Name | Payload |
|---|---|---|
| 0x30 | BSR_CONFIG | BSR length, opcodes (SAMPLE, PRELOAD, EXTEST, BYPASS), safe vector |
| 0x31 | BSR_SAMPLE | → one capture vector |
| 0x32 | BSR_STREAM_START | u32 interval µs (0 = as fast as possible), u8 mode (0 every sample, 1 changes only), optional bit mask |
| 0x33 | BSR_STREAM_STOP | — |
| 0x34 | BSR_EXTEST_ENTER | — (preloads the safe vector first, then EXTEST) |
| 0x35 | BSR_SET | list of (cell index, value), applied as one update; capture returned |
| 0x36 | BSR_EXTEST_EXIT | — (back to SAMPLE or BYPASS) |

Stream frames (device → host, `cmd` 0x32, unsolicited):

```
u32 timestamp_us_low; u16 timestamp_us_high; u16 n_changes_or_flag;
then either a full vector (mode 0) or n × u16 cell index with value in bit 15 (mode 1)
```

Device-side sample rate is limited by BSR length / TCK plus TAP overhead. Example: 1000 cells at 10 MHz ≈ 110 µs per capture, so about 8,000 captures/s. In change-only mode USB bandwidth is not a concern. Also report dropped captures (counter in frame header) so the host can display them.

**Bus engine (parallel memory via boundary scan)**

| Code | Name | Payload |
|---|---|---|
| 0x40 | BUS_CONFIG | address cell list (A0..An), data cell list + data control cells, CE/OE/WE/BYTE/WP/RESET cells with polarity, bus width 8/16 |
| 0x41 | BUS_WRITE | list of (address, data) bus cycles |
| 0x42 | BUS_READ | start address, count → data |
| 0x43 | FLASH_CFI_QUERY | → raw CFI table + decoded JEDEC IDs |
| 0x44 | FLASH_ERASE | algorithm (AMD/JEDEC 0x0002, Intel 0x0001/0x0003), sector address or chip |
| 0x45 | FLASH_PROGRAM | algorithm, start address, data block (streamed in fragments) |
| 0x46 | FLASH_PROGRESS | → bytes done, current state, last error |
| 0x47 | FLASH_ABORT | — |

One bus write cycle = BSR updates: (1) set address, data and CE active, WE inactive; (2) WE active; (3) WE inactive. Read cycle = set address with OE active, then capture data. The firmware modifies only the affected bits in the stored vector and shifts with write-only DMA when no capture is needed.

Implement status polling (DQ7 data polling / DQ6 toggle for AMD, SR.7 for Intel) on the device with a timeout.

**SPI**

| Code | Name | Payload |
|---|---|---|
| 0x50 | SPI_CONFIG | mode, clock Hz, CS polarity |
| 0x51 | SPI_XFER | TX bytes, u8 flags (keep CS) → RX bytes |
| 0x52 | SPIFLASH_PROGRAM | page program loop with WREN / RDSR polling, streamed data |
| 0x53 | SPIJ_CONFIG | SPI-over-JTAG bridge: USER opcode, frame format (header bits, bit order) |
| 0x54 | SPIJ_XFER / SPIJ_PROGRAM | like 0x51 / 0x52, but routed through the FPGA bridge bitstream |

SPI direct uses the second PIO SM and the separate SPI header. SPIJ uses the JTAG engine. Same page-program loop, different transport.

### 5.3 Error codes

`0 OK, 1 BAD_CMD, 2 BAD_LEN, 3 NOT_CONFIGURED, 4 VTREF_LOW, 5 IO_DISABLED, 6 TDO_STUCK (all 0 or all 1), 7 CHECK_MISMATCH, 8 TIMEOUT, 9 FLASH_ERROR (status bit), 10 BUSY, 11 ABORTED, 12 BUFFER_OVERFLOW`

---

## 6. Safety rules (firmware-enforced)

1. Target-side outputs stay disabled until VTref is valid and the host sends IO_ENABLE.
2. EXTEST is only entered after the safe vector has been loaded with PRELOAD.
3. On USB disconnect, host timeout (configurable, default 2 s without traffic during EXTEST) or VTref loss: preload safe vector, leave EXTEST, TAP reset, disable outputs.
4. Abort is always processed immediately, even during long flash operations.

---

## 7. Repository layout

```
rpjtag/
├── hw/                     KiCad project, BOM, adapter boards
├── fw/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c
│   │   ├── usb/            descriptors, TinyUSB callbacks
│   │   ├── dap/            CMSIS-DAP command handling
│   │   ├── native/         native protocol parser and dispatcher
│   │   ├── jtag/           PIO programs (.pio), TAP state machine, chain
│   │   ├── bsr/            boundary-scan engine, stream
│   │   ├── bus/            bus engine, CFI, AMD/Intel algorithms
│   │   ├── spi/            SPI direct and SPI-over-JTAG
│   │   └── board/          pins, ADC, LEDs, power, safety supervisor
│   └── test/               unit tests that run on the host (TAP state machine, framing)
├── tools/
│   ├── rpjtag.py           Python host library (pyusb) for testing the native protocol
│   └── tests/              hardware-in-the-loop tests
└── docs/
    └── protocol.md         generated or maintained from section 5
```

---

## 8. Milestones

Each milestone ends with the listed acceptance checks passing.

### M0 — Bring-up
- Pico SDK project for the RP2354B, TinyUSB enumerates as a vendor device, LEDs, button, VTref via ADC.
- **Accept:** device enumerates on macOS (`ioreg -p IOUSB`); `tools/rpjtag.py info` prints version and VTref.

### M1 — JTAG engine
- PIO shift program with DMA, TAP state machine, TCK setting, IDCODE scan.
- Host-side unit tests for the TAP state machine and shortest-path table.
- **Accept:** reads the correct IDCODE from a test target (e.g. 7-series FPGA board or XC9500XL CPLD) at 1, 6 and 15 MHz; logic analyzer trace of TCK/TMS/TDI matches expectations.

### M2 — CMSIS-DAP v2
- CMSIS-DAP interface with the JTAG command set from section 3.4.
- **Accept:** `openocd -f interface/cmsis-dap.cfg -c "transport select jtag" ...` detects the chain; `openFPGALoader -c cmsisdap --detect` identifies the FPGA; loading a bitstream into SRAM works.

### M3 — Native protocol + chain
- Framing, pipelining, TAP primitives, BATCH, CHAIN_DETECT/CONFIG with automatic BYPASS padding.
- Python library covers all commands so far.
- **Accept:** two-device chain (e.g. FPGA + CPLD) detected; scans addressed to device 2 work without manual padding.

### M4 — Boundary scan + live sampling
- BSR_CONFIG, SAMPLE, stream in both modes, dropped-capture counter.
- **Accept:** toggling a pin on the target is visible in the stream; measured capture rate is within 20 % of the calculated value; a 1-hour streaming run without lost frames in change-only mode.

### M5 — EXTEST + bus engine + parallel NOR
- EXTEST with safety supervisor, BSR_SET, bus engine, CFI query, AMD and Intel algorithms with device-side status polling.
- **Accept:** CFI query reads correct values from a parallel NOR (e.g. S29GL or JS28F) on a board with a BSDL-described device; erase / program / verify of 1 MB succeeds; unplugging USB during EXTEST leaves the target in a safe state (measured).

### M6 — SPI direct + SPI over JTAG + UART bridge
- SPI direct engine, SPI flash page-program loop, SPIJ transport with a bridge bitstream, CDC UART.
- **Accept:** reading the JEDEC ID and programming a 4 MB SPI flash both directly and via a 7-series bridge bitstream; verify passes; UART bridge works at 115200 and 1 Mbaud.

### M7 — SWD (optional)
- CMSIS-DAP SWD transport.
- **Accept:** OpenOCD flashes an STM32 via SWD.

### M8 — Hardening
- Firmware update path (BOOTSEL via REBOOT command, UF2), watchdog, error counters, fuzzing of the native parser (host-side), documentation.
- **Accept:** fuzzing run of 10⁶ random frames without crash or hang; all safety rules from section 6 verified on the bench.

---

## 9. Later ideas

- Standalone mode: image stored in internal flash or external PSRAM / microSD, programming started by button press.
- QSPI PSRAM on the second QMI chip select for images up to 8 MB (check the RP2354 datasheet for the CS1 pin).
- 5 V adapter board with different buffers.
- JTAG-based target voltage auto-adjustment for ports that output their own VTref.

---

## 10. Notes for Claude Code

- Prefer small, testable modules. The TAP state machine, protocol framing and bus-cycle generation must have host-side unit tests that run without hardware.
- Keep `docs/protocol.md` in sync with any protocol change; bump the protocol version in GET_INFO.
- Don't copy code from GPL projects (OpenOCD, UrJTAG, xc3sprog). Apache-2.0 (CMSIS-DAP reference, openFPGALoader) and MIT (debugprobe) sources may be reused with attribution.
- Timing-critical code goes into RAM. Measure with a logic analyzer before optimizing.
- Every milestone ends with a short `docs/milestones/Mx.md` noting what was tested and how.
