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

    def test_bsr_config_encodes_metadata_and_safe_vector(self) -> None:
        transport = FakeBulkTransport(response_frame(0x30, 0, b""))
        client = NativeProtocolClient(transport)

        client.bsr_config(10, 0x01, 0x01, 0x26, 0x3F, b"\x55\x01")

        request = NativeFrame.decode(transport.writes[0])
        self.assertEqual(request.payload,
                         struct.pack("<HIIII", 10, 1, 1, 0x26, 0x3F) + b"\x55\x01")

    def test_bsr_sample_returns_packed_capture(self) -> None:
        capture = bytes((index & 0xFF for index in range(43)))
        transport = FakeBulkTransport(response_frame(0x31, 0, capture))

        self.assertEqual(NativeProtocolClient(transport).bsr_sample(339), capture)
        self.assertEqual(NativeFrame.decode(transport.writes[0]).payload, b"")

    def test_bsr_stream_start_encodes_mode_and_mask(self) -> None:
        transport = FakeBulkTransport(
            response_frame(0x30, 0, b"") + response_frame(0x32, 1, b"")
        )
        client = NativeProtocolClient(transport)
        client.bsr_config(10, 1, 1, 0x26, 0x3F, bytes(2))

        client.bsr_stream_start(5000, mode=1, mask=b"\x03\x02")

        request = NativeFrame.decode(transport.writes[1])
        self.assertEqual(request.payload, struct.pack("<IB", 5000, 1) + b"\x03\x02")

    def test_stream_event_before_start_ack_is_queued(self) -> None:
        vector = bytes((index & 0xFF for index in range(43)))
        stream_payload = struct.pack("<IHHI", 0x12345678, 2, 0xFFFF, 4) + vector
        responses = (
            response_frame(0x30, 0, b"")
            + NativeFrame(0x32, 0, 0, stream_payload).encode()
            + response_frame(0x32, 1, b"")
        )
        client = NativeProtocolClient(FakeBulkTransport(responses))
        client.bsr_config(339, 1, 1, 0x26, 0x3F, bytes(43))

        client.bsr_stream_start(1000, mode=1)
        frame = client.read_bsr_stream()

        self.assertEqual(frame.timestamp_us, 0x212345678)
        self.assertEqual(frame.dropped_captures, 4)
        self.assertEqual(frame.full_vector, vector)

    def test_change_only_stream_decodes_index_and_value(self) -> None:
        delta = struct.pack("<HH", 2, 0x8005)
        payload = struct.pack("<IHHI", 10, 0, 2, 7) + delta
        client = NativeProtocolClient(FakeBulkTransport(NativeFrame(0x32, 8, 0, payload).encode()))
        client._bsr_bit_count = 16

        frame = client.read_bsr_stream()

        self.assertIsNone(frame.full_vector)
        self.assertEqual(frame.changes, {2: False, 5: True})
        self.assertEqual(frame.dropped_captures, 7)

    def test_nonzero_status_raises_protocol_error(self) -> None:
        transport = FakeBulkTransport(response_frame(0x10, 0, b"", status=6))

        with self.assertRaises(NativeProtocolError) as error:
            NativeProtocolClient(transport).tap_reset()
        self.assertEqual(error.exception.status, 6)


if __name__ == "__main__":
    unittest.main()