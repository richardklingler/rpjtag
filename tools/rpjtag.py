#!/usr/bin/env python3
"""Minimal native-protocol helper for the rpjtag firmware project."""

from __future__ import annotations

import struct
from dataclasses import dataclass


@dataclass
class MsgHeader:
    cmd: int
    seq: int
    flags: int = 0
    length: int = 0

    @classmethod
    def encode(cls, cmd: int, seq: int, payload: bytes = b"", flags: int = 0) -> bytes:
        header = struct.pack("<BBHI", cmd, seq, flags, len(payload))
        return header + payload

    @classmethod
    def decode(cls, frame: bytes) -> tuple["MsgHeader", bytes]:
        if len(frame) < 8:
            raise ValueError("frame too short for a valid message header")
        cmd, seq, flags, length = struct.unpack("<BBHI", frame[:8])
        payload = frame[8:8 + length]
        return cls(cmd=cmd, seq=seq, flags=flags, length=length), payload


class RpjtagClient:
    """Small convenience wrapper for future host-side testing against the adapter."""

    def __init__(self, device=None):
        self.device = device

    def get_info(self) -> dict[str, int | str]:
        return {
            "fw_version": 0x0100,
            "hw_rev": 0x01,
            "max_bsr_bits": 4096,
            "protocol": "rpjtag native",
        }

    def set_tck(self, hz: int) -> int:
        return hz


if __name__ == "__main__":
    client = RpjtagClient()
    print(client.get_info())
