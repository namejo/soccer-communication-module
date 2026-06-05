"""Small payload codecs for SIBCP messages and services."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any, Mapping

MAX_LENGTH_PREFIXED_BYTES = 0xFFFF


class CodecError(ValueError):
    """Raised when a payload cannot be encoded or decoded by a codec."""


class Codec:
    """Base class for SIBCP payload codecs."""

    size: int | None = None

    def encode(self, value: Any) -> bytes:
        raise NotImplementedError

    def decode(self, payload: bytes | bytearray | memoryview) -> Any:
        data = bytes(payload)
        value, offset = self.decode_from(data, 0)
        if offset != len(data):
            raise CodecError(f"trailing {len(data) - offset} byte(s) after payload")
        return value

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[Any, int]:
        raise NotImplementedError


class EmptyCodec(Codec):
    size = 0

    def encode(self, value: Any = None) -> bytes:
        if value is not None:
            raise CodecError("empty payload expects None")
        return b""

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[None, int]:
        if offset > len(payload):
            raise CodecError("offset exceeds payload length")
        return None, offset

    def __repr__(self) -> str:
        return "Empty"


class BoolCodec(Codec):
    size = 1

    def encode(self, value: Any) -> bytes:
        if value not in (False, True, 0, 1):
            raise CodecError("bool payload expects True or False")
        return bytes([1 if bool(value) else 0])

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[bool, int]:
        if offset + 1 > len(payload):
            raise CodecError("bool payload is missing one byte")
        value = int(payload[offset])
        if value not in (0, 1):
            raise CodecError(f"invalid bool value 0x{value:02x}")
        return bool(value), offset + 1

    def __repr__(self) -> str:
        return "Bool"


class NumericCodec(Codec):
    def __init__(self, fmt: str, name: str) -> None:
        self._struct = struct.Struct("<" + fmt)
        self.size = self._struct.size
        self.name = name

    def encode(self, value: Any) -> bytes:
        try:
            return self._struct.pack(value)
        except struct.error as exc:
            raise CodecError(f"{self.name} payload cannot encode {value!r}") from exc

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[Any, int]:
        if offset + self._struct.size > len(payload):
            raise CodecError(f"{self.name} payload is too short")
        return self._struct.unpack_from(payload, offset)[0], offset + self._struct.size

    def __repr__(self) -> str:
        return self.name


class StringCodec(Codec):
    """UTF-8 string with a uint16 little-endian byte length prefix."""

    size = None

    def encode(self, value: Any) -> bytes:
        if not isinstance(value, str):
            raise CodecError("string payload expects str")
        encoded = value.encode("utf-8")
        if len(encoded) > MAX_LENGTH_PREFIXED_BYTES:
            raise CodecError("string payload exceeds uint16 length prefix")
        return len(encoded).to_bytes(2, "little") + encoded

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[str, int]:
        if offset + 2 > len(payload):
            raise CodecError("string payload is missing length prefix")
        length = int(payload[offset]) | (int(payload[offset + 1]) << 8)
        start = offset + 2
        end = start + length
        if end > len(payload):
            raise CodecError("string payload is shorter than its length prefix")
        try:
            return bytes(payload[start:end]).decode("utf-8"), end
        except UnicodeDecodeError as exc:
            raise CodecError("string payload is not valid UTF-8") from exc

    def __repr__(self) -> str:
        return "String"


class BytesCodec(Codec):
    """Raw bytes with a uint16 little-endian byte length prefix."""

    size = None

    def encode(self, value: Any) -> bytes:
        if not isinstance(value, (bytes, bytearray, memoryview)):
            raise CodecError("bytes payload expects bytes-like value")
        raw = bytes(value)
        if len(raw) > MAX_LENGTH_PREFIXED_BYTES:
            raise CodecError("bytes payload exceeds uint16 length prefix")
        return len(raw).to_bytes(2, "little") + raw

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[bytes, int]:
        if offset + 2 > len(payload):
            raise CodecError("bytes payload is missing length prefix")
        length = int(payload[offset]) | (int(payload[offset + 1]) << 8)
        start = offset + 2
        end = start + length
        if end > len(payload):
            raise CodecError("bytes payload is shorter than its length prefix")
        return bytes(payload[start:end]), end

    def __repr__(self) -> str:
        return "Bytes"


@dataclass(frozen=True)
class Field:
    name: str
    codec: Codec


def field(name: str, codec: Codec) -> Field:
    return Field(name=name, codec=codec)


class Struct(Codec):
    """A packed, ordered set of named fields.

    Encoded structs are equivalent to concatenating each field codec in order.
    Decoding returns a plain dict.
    """

    def __init__(self, *fields: Field | tuple[str, Codec]) -> None:
        if not fields:
            raise CodecError("Struct requires at least one field")

        normalized: list[Field] = []
        names: set[str] = set()
        for item in fields:
            if isinstance(item, Field):
                current = item
            else:
                name, codec = item
                current = Field(name=name, codec=codec)
            if not current.name:
                raise CodecError("Struct field name must not be empty")
            if current.name in names:
                raise CodecError(f"duplicate Struct field {current.name!r}")
            if not isinstance(current.codec, Codec):
                raise CodecError(f"field {current.name!r} does not use a Codec")
            normalized.append(current)
            names.add(current.name)

        self.fields = tuple(normalized)
        if all(item.codec.size is not None for item in self.fields):
            self.size = sum(int(item.codec.size) for item in self.fields)
        else:
            self.size = None

    def encode(self, value: Any) -> bytes:
        chunks: list[bytes] = []
        for item in self.fields:
            chunks.append(item.codec.encode(_field_value(value, item.name)))
        return b"".join(chunks)

    def decode_from(
        self, payload: bytes | bytearray | memoryview, offset: int = 0
    ) -> tuple[dict[str, Any], int]:
        result: dict[str, Any] = {}
        current_offset = offset
        for item in self.fields:
            value, current_offset = item.codec.decode_from(payload, current_offset)
            result[item.name] = value
        return result, current_offset

    def __repr__(self) -> str:
        fields = ", ".join(f"{item.name}: {item.codec!r}" for item in self.fields)
        return f"Struct({fields})"


def _field_value(value: Any, name: str) -> Any:
    if isinstance(value, Mapping):
        if name not in value:
            raise CodecError(f"missing Struct field {name!r}")
        return value[name]
    if hasattr(value, name):
        return getattr(value, name)
    raise CodecError(f"missing Struct field {name!r}")


Empty = EmptyCodec()
Bool = BoolCodec()
UInt8 = NumericCodec("B", "UInt8")
Int8 = NumericCodec("b", "Int8")
UInt16 = NumericCodec("H", "UInt16")
Int16 = NumericCodec("h", "Int16")
UInt32 = NumericCodec("I", "UInt32")
Int32 = NumericCodec("i", "Int32")
UInt64 = NumericCodec("Q", "UInt64")
Int64 = NumericCodec("q", "Int64")
Float32 = NumericCodec("f", "Float32")
String = StringCodec()
Bytes = BytesCodec()
