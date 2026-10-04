import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.rpjtag import RpjtagClient


class FakeSerialDevice:
    def __init__(self, responses: list[bytes]):
        self.responses = iter(responses)
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        return next(self.responses, b"")


class RpjtagClientTests(unittest.TestCase):
    def test_get_info_skips_console_telemetry(self) -> None:
        device = FakeSerialDevice([
            b"blink=3 vtref_mv=1200 button=0\r\n",
            b"RPJTAG_INFO fw_version=0x0100 hw_rev=0x01 vtref_mv=1200 button_pressed=1\r\n",
        ])

        info = RpjtagClient(device).get_info()

        self.assertEqual(device.writes, [b"INFO\n"])
        self.assertEqual(info, {
            "fw_version": 0x0100,
            "hw_rev": 0x01,
            "vtref_mv": 1200,
            "button_pressed": 1,
        })

    def test_get_vtref_returns_millivolts(self) -> None:
        device = FakeSerialDevice([b"RPJTAG_VTREF mv=1800\r\n"])

        self.assertEqual(RpjtagClient(device).get_vtref_mv(), 1800)
        self.assertEqual(device.writes, [b"VTREF\n"])

    def test_scan_idcode_parses_hex_value(self) -> None:
        device = FakeSerialDevice([b"RPJTAG_IDCODE idcode=0x12345679\r\n"])

        self.assertEqual(RpjtagClient(device).scan_idcode(), 0x12345679)
        self.assertEqual(device.writes, [b"IDCODE\n"])

    def test_set_tck_parses_applied_frequency(self) -> None:
        device = FakeSerialDevice([b"RPJTAG_TCK hz=6000000\r\n"])

        self.assertEqual(RpjtagClient(device).set_tck(6000000), 6000000)
        self.assertEqual(device.writes, [b"TCK 6000000\n"])

    def test_timeout_when_firmware_does_not_respond(self) -> None:
        with self.assertRaises(TimeoutError):
            RpjtagClient(FakeSerialDevice([])).get_info()


if __name__ == "__main__":
    unittest.main()