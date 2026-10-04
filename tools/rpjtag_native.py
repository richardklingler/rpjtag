#!/usr/bin/env python3
"""Native USB protocol client for the rpJTAG adapter."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from typing import Any, Protocol


HEADER = struct.Struct("<BBHI")
USB_VID = 0x1209
USB_PID = 0x5306
NATIVE_INTERFACE_NAME = "rpjtag native"
MAX_SCAN_BITS = 4096


@dataclass(frozen=True)
class NativeFrame:
    cmd: int
    seq: int
    flags: int
    payload: bytes

    def encode(self) -> bytes:
        if not 0 <= self.cmd <= 0xFF or not 0 <= self.seq <= 0xFF:
            raise ValueError("command and sequence must fit in one byte")
        if not 0 <= self.flags <= 0xFFFF:
            raise ValueError("flags must fit in two bytes")
        return HEADER.pack(self.cmd, self.seq, self.flags, len(self.payload)) + self.payload

    @classmethod
    def decode(cls, frame: bytes) -> NativeFrame:
        if len(frame) < HEADER.size:
            raise ValueError("frame is shorter than its header")
        cmd, seq, flags, length = HEADER.unpack_from(frame)
        if len(frame) != HEADER.size + length:
            raise ValueError("frame length does not match its header")
        return cls(cmd, seq, flags, frame[HEADER.size:])


class BulkTransport(Protocol):
    def write(self, data: bytes, timeout: int | None = None) -> int: ...

    def read(self, size: int, timeout: int | None = None) -> bytes: ...


class NativeProtocolError(RuntimeError):
    def __init__(self, status: int, response: bytes):
        super().__init__(f"native command failed with status {status}")
        self.status = status
        self.response = response


class NativeProtocolClient:
    def __init__(self, transport: BulkTransport, timeout_ms: int = 3000):
        self.transport = transport
        self.timeout_ms = timeout_ms
        self._sequence = 0
        self._read_buffer = bytearray()

    def _read_exact(self, length: int) -> bytes:
        while len(self._read_buffer) < length:
            chunk = self.transport.read(64, self.timeout_ms)
            if not chunk:
                raise TimeoutError("timed out reading native USB response")
            self._read_buffer.extend(chunk)
        result = bytes(self._read_buffer[:length])
        del self._read_buffer[:length]
        return result

    def request(self, cmd: int, payload: bytes = b"") -> bytes:
        sequence = self._sequence
        self._sequence = (self._sequence + 1) & 0xFF
        frame = NativeFrame(cmd, sequence, 0, payload).encode()
        written = self.transport.write(frame, self.timeout_ms)
        if written != len(frame):
            raise IOError(f"short USB write: {written} of {len(frame)} bytes")

        header = self._read_exact(HEADER.size)
        response_cmd, response_seq, flags, response_length = HEADER.unpack(header)
        if response_cmd != cmd or response_seq != sequence or flags != 0:
            raise ValueError("response command, sequence, or flags did not match request")
        if response_length == 0:
            raise ValueError("native response is missing its status byte")
        response = self._read_exact(response_length)
        status, body = response[0], response[1:]
        if status != 0:
            raise NativeProtocolError(status, body)
        return body

    def get_info(self) -> dict[str, int | bytes]:
        response = self.request(0x01)
        info = struct.Struct("<HBBIIHHB")
        if len(response) < info.size:
            raise ValueError("GET_INFO response is too short")
        firmware, hardware, _reserved, capabilities, max_scan, max_payload, response_capacity, serial_length = info.unpack_from(response)
        serial = response[info.size:]
        if len(serial) != serial_length:
            raise ValueError("GET_INFO serial length does not match response")
        return {
            "firmware_version": firmware,
            "hardware_revision": hardware,
            "capabilities": capabilities,
            "max_scan_bits": max_scan,
            "max_payload": max_payload,
            "response_capacity": response_capacity,
            "serial": serial,
        }

    def get_vtref(self) -> tuple[int, int]:
        response = self.request(0x03)
        if len(response) != 4:
            raise ValueError("GET_VTREF response must contain two u16 values")
        return struct.unpack("<HH", response)

    def set_tck(self, frequency_hz: int) -> int:
        response = self.request(0x02, struct.pack("<I", frequency_hz))
        if len(response) != 4:
            raise ValueError("SET_TCK response must contain one u32 value")
        return struct.unpack("<I", response)[0]

    def tap_reset(self) -> None:
        self.request(0x10)

    def tap_goto(self, state: int) -> None:
        self.request(0x11, bytes((state,)))

    def scan_dr(self, bit_count: int, tdi: bytes, end_state: int = 1,
                capture: bool = True) -> bytes | None:
        if not 1 <= bit_count <= MAX_SCAN_BITS:
            raise ValueError(f"scan length must be between 1 and {MAX_SCAN_BITS} bits")
        byte_count = (bit_count + 7) // 8
        if len(tdi) != byte_count:
            raise ValueError("TDI length does not match scan bit count")
        payload = struct.pack("<IBB", bit_count, end_state, int(capture)) + tdi
        response = self.request(0x13, payload)
        if not capture:
            if response:
                raise ValueError("SCAN_DR returned data when capture was disabled")
            return None
        if len(response) != byte_count:
            raise ValueError("SCAN_DR response length does not match scan bit count")
        return response

    def chain_detect(self) -> tuple[list[int], int]:
        response = self.request(0x20)
        if not response:
            raise ValueError("CHAIN_DETECT response is empty")
        count = response[0]
        expected_length = 1 + 4 * count + 2
        if count > 8 or len(response) != expected_length:
            raise ValueError("CHAIN_DETECT response length is invalid")
        idcodes = [struct.unpack_from("<I", response, 1 + 4 * index)[0]
                   for index in range(count)]
        ir_length = struct.unpack_from("<H", response, 1 + 4 * count)[0]
        return idcodes, ir_length

    def chain_config(self, ir_lengths: list[int], bypass_opcodes: list[int],
                     active_device: int = 0) -> None:
        if not ir_lengths or len(ir_lengths) != len(bypass_opcodes):
            raise ValueError("chain configuration requires matching non-empty device lists")
        if len(ir_lengths) > 8 or not 0 <= active_device < len(ir_lengths):
            raise ValueError("active device or chain length is out of range")
        payload = bytearray((len(ir_lengths), active_device))
        for ir_length, bypass_opcode in zip(ir_lengths, bypass_opcodes):
            if not 1 <= ir_length <= 32 or not 0 <= bypass_opcode <= 0xFFFFFFFF:
                raise ValueError("IR lengths and BYPASS opcodes are out of range")
            payload.extend(struct.pack("<BI", ir_length, bypass_opcode))
        self.request(0x21, bytes(payload))


class PyUsbBulkTransport:
    def __init__(self, device: Any, out_endpoint: Any,
                 in_endpoint: Any, interface_number: int, timeout_ms: int):
        self.device = device
        self.out_endpoint = out_endpoint
        self.in_endpoint = in_endpoint
        self.interface_number = interface_number
        self.timeout_ms = timeout_ms

    def write(self, data: bytes, timeout: int | None = None) -> int:
        return int(self.out_endpoint.write(data, timeout=timeout or self.timeout_ms))

    def read(self, size: int, timeout: int | None = None) -> bytes:
        return bytes(self.in_endpoint.read(size, timeout=timeout or self.timeout_ms))

    def close(self) -> None:
        import usb.util

        usb.util.release_interface(self.device, self.interface_number)
        usb.util.dispose_resources(self.device)


def open_native_device(timeout_ms: int = 3000) -> PyUsbBulkTransport:
    try:
        import usb.core
        import usb.util
    except ImportError as error:
        raise SystemExit("Install host dependencies with: python3 -m pip install -r requirements.txt") from error

    device = usb.core.find(idVendor=USB_VID, idProduct=USB_PID)
    if device is None:
        raise RuntimeError(f"rpJTAG {USB_VID:04x}:{USB_PID:04x} was not found")
    device.set_configuration()
    configuration = device.get_active_configuration()
    for interface in configuration:
        if interface.bInterfaceClass != 0xFF:
            continue
        if usb.util.get_string(device, interface.iInterface) != NATIVE_INTERFACE_NAME:
            continue
        interface_number = interface.bInterfaceNumber
        try:
            kernel_driver_active = device.is_kernel_driver_active(interface_number)
        except NotImplementedError:
            kernel_driver_active = False
        if kernel_driver_active:
            device.detach_kernel_driver(interface_number)
        usb.util.claim_interface(device, interface_number)
        endpoints = [endpoint for endpoint in interface
                     if usb.util.endpoint_type(endpoint.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK]
        out_endpoint = next((endpoint for endpoint in endpoints
                             if usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_OUT), None)
        in_endpoint = next((endpoint for endpoint in endpoints
                            if usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_IN), None)
        if out_endpoint is None or in_endpoint is None:
            usb.util.release_interface(device, interface_number)
            raise RuntimeError("native interface is missing a bulk endpoint")
        return PyUsbBulkTransport(device, out_endpoint, in_endpoint,
                                  interface_number, timeout_ms)
    usb.util.dispose_resources(device)
    raise RuntimeError(f"USB interface {NATIVE_INTERFACE_NAME!r} was not found")


def main() -> None:
    parser = argparse.ArgumentParser(description="Query rpJTAG's native USB interface")
    parser.add_argument("command", choices=("info", "vtref", "tck", "reset", "detect", "scan-dr"))
    parser.add_argument("--timeout", type=int, default=3000, help="USB timeout in milliseconds")
    parser.add_argument("--hz", type=int, help="TCK frequency for the tck command")
    parser.add_argument("--bits", type=int, help="scan length for scan-dr")
    parser.add_argument("--tdi", type=lambda value: int(value, 0), default=0,
                        help="little-endian TDI bit pattern for scan-dr")
    arguments = parser.parse_args()
    if arguments.command == "tck" and arguments.hz is None:
        parser.error("tck requires --hz")
    if arguments.command == "scan-dr" and arguments.bits is None:
        parser.error("scan-dr requires --bits")

    transport = open_native_device(arguments.timeout)
    try:
        client = NativeProtocolClient(transport, arguments.timeout)
        if arguments.command == "info":
            info = client.get_info()
            info["serial"] = bytes(info["serial"]).hex()
            print(json.dumps(info, indent=2))
        elif arguments.command == "vtref":
            voltage_mv, current_ma = client.get_vtref()
            print(json.dumps({"vtref_mv": voltage_mv, "target_current_ma": current_ma}, indent=2))
        elif arguments.command == "tck":
            print(f"{client.set_tck(arguments.hz)} Hz")
        elif arguments.command == "reset":
            client.tap_reset()
            print("TAP reset")
        elif arguments.command == "detect":
            idcodes, ir_length = client.chain_detect()
            print(json.dumps({"idcodes": [f"0x{idcode:08X}" for idcode in idcodes],
                              "total_ir_bits": ir_length}, indent=2))
        else:
            bit_count = arguments.bits
            tdi = arguments.tdi.to_bytes((bit_count + 7) // 8, "little")
            tdo = client.scan_dr(bit_count, tdi)
            print(bytes(tdo).hex())
    finally:
        transport.close()


if __name__ == "__main__":
    main()