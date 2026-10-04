from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.rpjtag_native import NativeFrame, NativeProtocolClient, NativeProtocolError


class FakeBulkTransport:
    def __init__(self, response: bytes, max_read: int = 64):
        self.response = bytearray(response)
        self.max_read = max_read
        self.writes: list[bytes] = []

    def write(self, data: bytes, timeout: int | None = None) -> int:
        self.writes.append(data)
        return len(data)

    def read(self, size: int, timeout: int | None = None) -> bytes:
        count = min(size, self.max_read, len(self.response))
        result = bytes(self.response[:count])
        del self.response[:count]
        return result


def response_frame(command: int, sequence: int, body: bytes, status: int = 0) -> bytes:
    return NativeFrame(command, sequence, 0, bytes((status,)) + body).encode()


class NativeFrameTests(unittest.TestCase):
    def test_round_trip_and_exact_length_validation(self) -> None:
        frame = NativeFrame(0x13, 9, 0, bytes(range(128)))

        self.assertEqual(NativeFrame.decode(frame.encode()), frame)
        with self.assertRaises(ValueError):
            NativeFrame.decode(frame.encode()[:-1])
        with self.assertRaises(ValueError):
            NativeFrame.decode(frame.encode() + b"x")


class NativeClientTests(unittest.TestCase):
    def test_get_info_reads_fragmented_usb_packets(self) -> None:
        body = struct.pack("<HBBIIHHB", 0x0100, 1, 0, 0x1F, 4096, 1552, 1553, 2) + b"\x12\x34"
        transport = FakeBulkTransport(response_frame(1, 0, body), max_read=3)

        info = NativeProtocolClient(transport).get_info()

        self.assertEqual(info["firmware_version"], 0x0100)
        self.assertEqual(info["capabilities"], 0x1F)
        self.assertEqual(info["serial"], b"\x12\x34")
        self.assertEqual(NativeFrame.decode(transport.writes[0]).cmd, 1)

    def test_scan_dr_encodes_header_and_returns_capture(self) -> None:
        transport = FakeBulkTransport(response_frame(0x13, 0, b"\xA5"))
        client = NativeProtocolClient(transport)

        self.assertEqual(client.scan_dr(8, b"\x3C"), b"\xA5")
        request = NativeFrame.decode(transport.writes[0])
        self.assertEqual(request.payload, struct.pack("<IBB", 8, 1, 1) + b"\x3C")

    def test_chain_detect_decodes_ids_and_ir_length(self) -> None:
        body = bytes((2,)) + struct.pack("<IIH", 0x03620093, 0, 12)
        transport = FakeBulkTransport(response_frame(0x20, 0, body))

        self.assertEqual(NativeProtocolClient(transport).chain_detect(),
                         ([0x03620093, 0], 12))

    def test_nonzero_status_raises_protocol_error(self) -> None:
        transport = FakeBulkTransport(response_frame(0x10, 0, b"", status=6))

        with self.assertRaises(NativeProtocolError) as error:
            NativeProtocolClient(transport).tap_reset()
        self.assertEqual(error.exception.status, 6)


if __name__ == "__main__":
    unittest.main()