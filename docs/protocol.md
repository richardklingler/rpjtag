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

USB interface 0 is a 64-byte bulk CMSIS-DAP v2 interface with the string `CMSIS-DAP v2`. The ARM CMSIS-DAP command processor is configured for JTAG; SWD is disabled. The default TCK is 1 MHz. JTAG sequences, clock selection, and IDCODE scans are routed to the Pico PIO/DMA engine. A Microsoft OS 2.0 descriptor advertises WinUSB for interface 0. USB CDC remains on a separate interface for diagnostics. OpenOCD 0.12.0 has been verified to discover the interface and scan the attached Spartan-7.

## CDC bring-up commands

The firmware accepts newline-terminated `INFO`, `VTREF`, `TCK <hz>`, and `IDCODE` commands over USB CDC. Responses begin with `RPJTAG_INFO`, `RPJTAG_VTREF`, `RPJTAG_TCK`, or `RPJTAG_IDCODE` and contain space-separated `key=value` fields. This diagnostic line protocol is temporary and is not the binary native protocol described above.
