"""Friendly schema helpers for the high-level RJSCM wrapper."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

from sibcp import (
    Bool,
    Bytes,
    Codec,
    DefinitionError,
    Empty,
    Float32,
    Int8,
    Int16,
    Int32,
    Int64,
    String,
    Struct,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
)

SchemaSpec = Any

_PYTHON_TYPES = {
    bool: Bool,
    int: Int32,
    float: Float32,
    str: String,
    bytes: Bytes,
}

_ALIASES = {
    "empty": Empty,
    "none": Empty,
    "bool": Bool,
    "boolean": Bool,
    "uint8": UInt8,
    "u8": UInt8,
    "byte": UInt8,
    "int8": Int8,
    "i8": Int8,
    "uint16": UInt16,
    "u16": UInt16,
    "int16": Int16,
    "i16": Int16,
    "uint32": UInt32,
    "u32": UInt32,
    "int32": Int32,
    "i32": Int32,
    "int": Int32,
    "uint64": UInt64,
    "u64": UInt64,
    "int64": Int64,
    "i64": Int64,
    "float": Float32,
    "float32": Float32,
    "string": String,
    "str": String,
    "bytes": Bytes,
}


def codec_from_schema(schema: SchemaSpec = None) -> Codec:
    """Convert a beginner-friendly schema description into a SIBCP codec.

    Accepted values:
    - `None`, `"empty"` for no payload
    - Python types such as `bool`, `int`, `float`, `str`, `bytes`
    - strings such as `"uint8"`, `"int16"`, `"int32"`, `"float32"`
    - dicts such as `{"x_mm": "int32", "visible": bool}`
    - ordered pairs such as `[("x_mm", "int32"), ("y_mm", "int32")]`
    """

    if schema is None:
        return Empty
    if isinstance(schema, Codec):
        return schema
    if isinstance(schema, type) and schema in _PYTHON_TYPES:
        return _PYTHON_TYPES[schema]
    if isinstance(schema, str):
        return _codec_from_string(schema)
    if isinstance(schema, Mapping):
        return _struct_from_fields(schema.items())
    if _looks_like_field_sequence(schema):
        return _struct_from_fields(schema)
    raise DefinitionError(f"unsupported schema definition {schema!r}")


def _codec_from_string(name: str) -> Codec:
    key = name.strip().lower().replace("-", "").replace("_", "")
    try:
        return _ALIASES[key]
    except KeyError as exc:
        known = ", ".join(sorted(_ALIASES))
        raise DefinitionError(
            f"unknown schema type {name!r}; expected one of {known}"
        ) from exc


def _struct_from_fields(fields: Any) -> Struct:
    normalized = []
    for raw_name, raw_schema in fields:
        name = str(raw_name)
        if not name:
            raise DefinitionError("field names must not be empty")
        normalized.append((name, codec_from_schema(raw_schema)))
    return Struct(*normalized)


def _looks_like_field_sequence(value: Any) -> bool:
    if isinstance(value, (str, bytes, bytearray, memoryview)):
        return False
    if not isinstance(value, Sequence):
        return False
    for item in value:
        if not isinstance(item, Sequence) or isinstance(item, (str, bytes, bytearray)):
            return False
        if len(item) != 2:
            return False
    return True
