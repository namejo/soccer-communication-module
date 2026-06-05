"""SIBCP frame encoding and streaming parsing.

The wire format mirrors firmware/RCj_comm_module/sibcp_protocol.{h,cpp}.
Multi-byte integers are little-endian and the CRC is CRC-16/CCITT with
initial value 0xFFFF.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Iterable

START_BYTE_0 = 0xAA
START_BYTE_1 = 0x55
HEADER_LENGTH = 8
CRC_LENGTH = 2
MAX_PAYLOAD_LENGTH = 240
MAX_FRAME_LENGTH = HEADER_LENGTH + MAX_PAYLOAD_LENGTH + CRC_LENGTH


class PacketType(IntEnum):
    TOPIC = 0x01
    SERVICE_REQUEST = 0x02
    SERVICE_RESPONSE = 0x03
    SERVICE_DISCOVERY = 0x04


class FrameError(ValueError):
    """Raised when a SIBCP frame is malformed or fails CRC validation."""


@dataclass(frozen=True)
class Frame:
    packet_type: int
    transaction_id: int
    identifier_id: int
    payload: bytes
    raw: bytes

    @property
    def payload_length(self) -> int:
        return len(self.payload)


def _read_u16_le(data: bytes | bytearray | memoryview, offset: int) -> int:
    return int(data[offset]) | (int(data[offset + 1]) << 8)


def _require_u8(value: int, name: str) -> int:
    if not 0 <= int(value) <= 0xFF:
        raise ValueError(f"{name} must fit in uint8")
    return int(value)


def _require_u16(value: int, name: str) -> int:
    if not 0 <= int(value) <= 0xFFFF:
        raise ValueError(f"{name} must fit in uint16")
    return int(value)


def crc16_ccitt(data: bytes | bytearray | memoryview) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= int(value) << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_frame(
    packet_type: int | PacketType,
    transaction_id: int,
    identifier_id: int,
    payload: bytes | bytearray | memoryview = b"",
) -> bytes:
    payload_bytes = bytes(payload)
    if len(payload_bytes) > MAX_PAYLOAD_LENGTH:
        raise ValueError(
            f"payload length {len(payload_bytes)} exceeds {MAX_PAYLOAD_LENGTH} bytes"
        )

    body = bytearray()
    body.append(_require_u8(int(packet_type), "packet_type"))
    body.extend(_require_u16(transaction_id, "transaction_id").to_bytes(2, "little"))
    body.append(_require_u8(identifier_id, "identifier_id"))
    body.extend(len(payload_bytes).to_bytes(2, "little"))
    body.extend(payload_bytes)

    crc = crc16_ccitt(body)
    return bytes([START_BYTE_0, START_BYTE_1]) + bytes(body) + crc.to_bytes(2, "little")


def decode_frame(data: bytes | bytearray | memoryview) -> Frame:
    raw = bytes(data)
    if len(raw) < HEADER_LENGTH + CRC_LENGTH:
        raise FrameError("frame is shorter than the minimum SIBCP length")
    if len(raw) > MAX_FRAME_LENGTH:
        raise FrameError("frame exceeds maximum SIBCP length")
    if raw[0] != START_BYTE_0 or raw[1] != START_BYTE_1:
        raise FrameError("frame does not start with SIBCP magic bytes")

    payload_length = _read_u16_le(raw, 6)
    if payload_length > MAX_PAYLOAD_LENGTH:
        raise FrameError("payload exceeds maximum SIBCP length")

    expected_length = HEADER_LENGTH + payload_length + CRC_LENGTH
    if len(raw) != expected_length:
        raise FrameError(
            f"frame length {len(raw)} does not match payload length {payload_length}"
        )

    expected_crc = _read_u16_le(raw, len(raw) - CRC_LENGTH)
    actual_crc = crc16_ccitt(raw[2 : len(raw) - CRC_LENGTH])
    if expected_crc != actual_crc:
        raise FrameError(
            f"CRC mismatch: expected 0x{expected_crc:04x}, got 0x{actual_crc:04x}"
        )

    return Frame(
        packet_type=raw[2],
        transaction_id=_read_u16_le(raw, 3),
        identifier_id=raw[5],
        payload=raw[HEADER_LENGTH : HEADER_LENGTH + payload_length],
        raw=raw,
    )


def packet_type_name(packet_type: int) -> str:
    try:
        return PacketType(packet_type).name
    except ValueError:
        return "UNKNOWN"


class StreamingParser:
    """Incrementally parse SIBCP frames from a byte stream."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self._expected_length = 0

    def reset(self) -> None:
        self._buffer.clear()
        self._expected_length = 0

    def feed(self, data: bytes | bytearray | memoryview | Iterable[int]) -> list[Frame]:
        frames: list[Frame] = []
        for value in data:
            frame = self.push_byte(int(value))
            if frame is not None:
                frames.append(frame)
        return frames

    def push_byte(self, value: int) -> Frame | None:
        byte = _require_u8(value, "byte")

        if len(self._buffer) == 0:
            if byte == START_BYTE_0:
                self._buffer.append(byte)
            return None

        if len(self._buffer) == 1:
            if byte == START_BYTE_1:
                self._buffer.append(byte)
            elif byte != START_BYTE_0:
                self.reset()
            return None

        if len(self._buffer) >= MAX_FRAME_LENGTH:
            self.reset()
            return None

        self._buffer.append(byte)

        if len(self._buffer) == HEADER_LENGTH:
            payload_length = _read_u16_le(self._buffer, 6)
            if payload_length > MAX_PAYLOAD_LENGTH:
                self.reset()
                return None
            self._expected_length = HEADER_LENGTH + payload_length + CRC_LENGTH

        if self._expected_length and len(self._buffer) == self._expected_length:
            try:
                return decode_frame(self._buffer)
            finally:
                self.reset()

        return None
