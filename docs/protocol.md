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
