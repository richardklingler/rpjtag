#!/usr/bin/env python3
"""Minimal native-protocol helper for the rpjtag firmware project."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from typing import Protocol


class SerialDevice(Protocol):
    def write(self, data: bytes) -> int: ...

    def flush(self) -> None: ...

    def readline(self) -> bytes: ...


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
    """Host client for the temporary M0 command interface over USB CDC."""

    def __init__(self, device: SerialDevice):
        self.device = device

    def _request(self, command: str, response_prefix: str) -> str:
        self.device.write(f"{command}\n".encode("ascii"))
        self.device.flush()

        while True:
            response = self.device.readline()
            if not response:
                raise TimeoutError(f"timed out waiting for {command} response")

            line = response.decode("ascii", errors="replace").strip()
            if line.startswith("RPJTAG_ERROR "):
                raise RuntimeError(line.removeprefix("RPJTAG_ERROR "))
            if line.startswith(response_prefix):
                return line.removeprefix(response_prefix)

    @staticmethod
    def _parse_fields(payload: str) -> dict[str, int | str]:
        fields: dict[str, int | str] = {}
        for field in payload.split():
            key, separator, value = field.partition("=")
            if not separator:
                continue
            try:
                fields[key] = int(value, 0)
            except ValueError:
                fields[key] = value
        return fields

    def get_info(self) -> dict[str, int | str]:
        return self._parse_fields(self._request("INFO", "RPJTAG_INFO "))

    def get_vtref_mv(self) -> int:
        fields = self._parse_fields(self._request("VTREF", "RPJTAG_VTREF "))
        value = fields.get("mv")
        if not isinstance(value, int):
            raise ValueError("VTREF response did not contain an integer millivolt value")
        return value

    def scan_idcode(self) -> int:
        fields = self._parse_fields(self._request("IDCODE", "RPJTAG_IDCODE "))
        value = fields.get("idcode")
        if not isinstance(value, int):
            raise ValueError("IDCODE response did not contain an integer IDCODE")
        return value

    def set_tck(self, hz: int) -> int:
        fields = self._parse_fields(self._request(f"TCK {hz}", "RPJTAG_TCK "))
        value = fields.get("hz")
        if not isinstance(value, int):
            raise ValueError("TCK response did not contain an integer frequency")
        return value


def main() -> None:
    parser = argparse.ArgumentParser(description="Query an rpJTAG adapter over USB CDC")
    parser.add_argument("command", choices=("info", "vtref", "idcode", "tck"))
    parser.add_argument("--port", required=True, help="USB CDC serial port, e.g. /dev/cu.usbmodemXXXX")
    parser.add_argument("--timeout", type=float, default=3.0, help="response timeout in seconds")
    parser.add_argument("--hz", type=int, help="TCK frequency for the tck command")
    arguments = parser.parse_args()
    if arguments.command == "tck" and arguments.hz is None:
        parser.error("tck requires --hz")

    try:
        import serial
    except ImportError as error:
        raise SystemExit("Install host dependencies with: python3 -m pip install -r requirements.txt") from error

    with serial.Serial(arguments.port, baudrate=115200, timeout=arguments.timeout) as device:
        client = RpjtagClient(device)
        if arguments.command == "info":
            print(json.dumps(client.get_info(), indent=2))
        elif arguments.command == "vtref":
            print(f"{client.get_vtref_mv()} mV")
        elif arguments.command == "idcode":
            print(f"0x{client.scan_idcode():08X}")
        else:
            print(f"{client.set_tck(arguments.hz)} Hz")


if __name__ == "__main__":
    main()
