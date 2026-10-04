# Native protocol overview

This document mirrors the framing and command layout described in `rpjtag-firmware.md` and is kept intentionally compact for host-side testing.

## Message framing

Each packet is encoded as little-endian bytes:

- `cmd`: 1 byte
- `seq`: 1 byte
- `flags`: 2 bytes
- `len`: 4 bytes
- payload: `len` bytes

The `flags` bitfield supports:

- bit 0: more fragments follow
- bit 1: no response needed

Responses echo the same command and sequence number and prefix the payload with a status byte.
The firmware reassembles consecutive fragments with the same command and sequence. The native parser accepts payloads up to 1552 bytes; scan commands are bounded to 4096 bits.

Status values are `0` OK, `1` bad command, `2` bad length, `3` not configured, `4` low VTref, `5` I/O disabled, `6` TDO stuck, `7` check mismatch, `8` timeout, `9` flash error, `10` busy, `11` aborted, and `12` buffer overflow.

## M3 payloads

All multibyte fields are little-endian. Response payloads below omit the leading status byte.

- `GET_INFO`: `u16 firmware version`, `u8 hardware revision`, `u8 reserved`, `u32 capabilities`, `u32 maximum scan bits`, `u16 maximum request payload`, `u16 response buffer capacity`, `u8 serial length`, then raw serial bytes. Capability bits 0-4 advertise JTAG, PIO scans, native framing, chain support, and batch support.
- `SET_TCK`: request `u32 Hz`; response `u32 applied Hz`.
- `GET_VTREF`: response `u16 millivolts`, `u16 target current mA` (zero when unavailable).
- `TAP_GOTO`: request one TAP-state index, using the 16 states in the order listed in `rpjtag-firmware.md`.
- `SCAN_IR`: request `u16 bits`, `u8 end state`, `u8 flags`, then `ceil(bits/8)` TDI bytes. `SCAN_DR` uses `u32 bits` before the same end-state and flags bytes. Flag bit 0 captures TDO; captured scans return `ceil(bits/8)` bytes.
- `SCAN_DR_CHECK`: request `u32 bits`, then TDI, expected TDO, and mask bit vectors. Response is `u32 first mismatch bit`, or `0xffffffff` when all masked bits match; mismatch also sets status 7.
- `RUNTEST`: request `u32 TCK cycles`, `u32 minimum microseconds`, `u8 end state`.
- `BATCH`: repeated `u8 command`, `u16 payload length`, payload records for commands `0x10`-`0x15`. Response is repeated `u8 command`, `u16 response length`, response payload (including each subcommand's status). At most 32 commands are accepted.
- `CHAIN_DETECT`: response `u8 device count`, one `u32 IDCODE` per device (`0` means BYPASS-only), then `u16 aggregate IR length`. Device index 0 is nearest TDO.
- `CHAIN_CONFIG`: request `u8 device count`, `u8 active device index`, then one record per device: `u8 IR length`, `u32 BYPASS opcode`. IR lengths are 1-32 bits; non-active devices are automatically padded for scans.
- `BSR_CONFIG`: request `u16 boundary bits`, `u32 SAMPLE opcode`, `u32 PRELOAD opcode`, `u32 EXTEST opcode`, `u32 BYPASS opcode`, then `ceil(bits/8)` safe-vector bytes. The active chain device must already be configured.
- `BSR_SAMPLE`: no request payload; returns the configured boundary register as `ceil(bits/8)` captured bytes using SAMPLE/PRELOAD. This operation does not select EXTEST.
- `BSR_STREAM_START`: request `u32 interval microseconds`, `u8 mode` (`0` full vector each sample, `1` change-only), then an optional `ceil(BSR bits/8)` cell mask. `interval=0` samples as fast as the firmware task loop permits.
- `BSR_STREAM_STOP`: no request payload.

Unsolicited stream frames use command `0x32`, flags 0, and payload `u32 timestamp_us_low`, `u16 timestamp_us_high`, `u16 change_count_or_flag`, `u32 cumulative_dropped_captures`, then data. `change_count_or_flag=0xffff` means a full vector follows; otherwise that many `u16` records follow, with bit 15 as value and bits 0-14 as the BSR cell index. Change-only mode emits a full baseline at start and after a dropped/oversized delta frame. Frames that exceed available TX space are skipped and counted. With the current 256-byte vendor TX FIFO, streaming is limited to BSRs of at most 1888 bits.

The included `hw/bsdl/xc7s15_ftgb196.bsd` declares XC7S15 FTGB196 with a 6-bit IR, 339-bit BSR, SAMPLE/PRELOAD `0x01`, EXTEST `0x26`, and BYPASS `0x3f`. The BSDL maps port `IO_A13` to package ball A13; BSR cell 307 is its output/control entry and cell 308 its input sample. On the Spartan Edge blinky test, A13/LED L2 toggled every approximately 0.5 seconds over 1,616 samples in 3.001 seconds (about 538 captures/s at the current TCK). The BSDL itself marks its data preliminary and unverified; confirm captures against known target signals before relying on other cell mappings.

The device enumerates as a composite USB device: CMSIS-DAP v2 is vendor interface 0 (`CMSIS-DAP v2`), native protocol is vendor interface 1 (`rpjtag native`), and CDC diagnostics occupy interfaces 2 and 3.

## Core commands

- `0x01` GET_INFO
- `0x02` SET_TCK
- `0x03` GET_VTREF
- `0x10` TAP_RESET
- `0x11` TAP_GOTO
- `0x12` SCAN_IR
- `0x13` SCAN_DR
- `0x30` BSR_CONFIG
- `0x31` BSR_SAMPLE
- `0x32` BSR_STREAM_START
- `0x40` BUS_CONFIG
- `0x50` SPI_CONFIG

The protocol is designed for pipelining and response matching by `seq`.

## CMSIS-DAP v2

USB interface 0 is a 64-byte bulk CMSIS-DAP v2 interface with the string `CMSIS-DAP v2`. The ARM CMSIS-DAP command processor is configured for JTAG; SWD is disabled. The default TCK is 1 MHz. JTAG sequences, clock selection, and IDCODE scans are routed to the Pico PIO/DMA engine. A Microsoft OS 2.0 descriptor advertises WinUSB for interface 0. The native vendor interface is 1; CDC diagnostics remain on interfaces 2/3. OpenOCD 0.12.0 has been verified to discover the CMSIS-DAP interface and scan the attached Spartan-7.

## CDC bring-up commands

The firmware accepts newline-terminated `INFO`, `VTREF`, `TCK <hz>`, `IDCODE`, and `NATIVE` commands over USB CDC. `NATIVE` reports received byte, parsed frame, and queued response counters for native-interface diagnosis. Responses begin with `RPJTAG_INFO`, `RPJTAG_VTREF`, `RPJTAG_TCK`, `RPJTAG_IDCODE`, or `RPJTAG_NATIVE` and contain space-separated `key=value` fields. This diagnostic line protocol is temporary and is not the binary native protocol described above.
