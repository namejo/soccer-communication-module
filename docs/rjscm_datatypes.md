# RJSCM Datatypes

Use these datatypes when defining custom Python `rjscm` messages and services.
Both robots must use the same name, ID, and schema.

The runnable example
[`python/sibcp/examples/all_datatypes_example.py`](../python/sibcp/examples/all_datatypes_example.py)
defines one topic publisher and one service for every datatype listed here.

Run it from the repository root:

```sh
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/all_datatypes_example.py
```

## Python Schema Types

| Schema | Aliases | Python value | Bytes | Meaning |
|--------|---------|--------------|-------|---------|
| `"empty"` | `"none"` | `None` | 0 | Event or service with no payload |
| `"bool"` | `"boolean"` | `True` / `False` | 1 | Boolean encoded as `0` or `1` |
| `"uint8"` | `"u8"`, `"byte"` | `int` | 1 | 0 to 255 |
| `"int8"` | `"i8"` | `int` | 1 | -128 to 127 |
| `"uint16"` | `"u16"` | `int` | 2 | 0 to 65535 |
| `"int16"` | `"i16"` | `int` | 2 | -32768 to 32767 |
| `"uint32"` | `"u32"` | `int` | 4 | 0 to 4294967295 |
| `"int32"` | `"i32"`, `"int"` | `int` | 4 | -2147483648 to 2147483647 |
| `"uint64"` | `"u64"` | `int` | 8 | 0 to 2^64 - 1 |
| `"int64"` | `"i64"` | `int` | 8 | -2^63 to 2^63 - 1 |
| `"float32"` | `"float"` | `float` | 4 | 32-bit little-endian float |
| `"string"` | `"str"` | `str` | variable | UTF-8 with a 2-byte length prefix |
| `"bytes"` | none | `bytes` | variable | Raw bytes with a 2-byte length prefix |
| `{"field": type}` | ordered pairs | `dict` | sum of fields | Struct made from the same datatypes |

Multi-byte numbers are little-endian. SIBCP frames currently allow a maximum
payload of 240 bytes, so keep strings, bytes, and structs small enough that the
encoded payload fits.

You may also use Python types as shortcuts:

| Python type | Same as |
|-------------|---------|
| `bool` | `"bool"` |
| `int` | `"int32"` |
| `float` | `"float32"` |
| `str` | `"string"` |
| `bytes` | `"bytes"` |

## Topic Publisher Pattern

Topics are one-way. They are good for sensor data or state that the peer robot
may read whenever it arrives.

```python
robot.define_message("battery_mv", id=0x30, schema="uint16")
robot.publish("battery_mv", 7400)
```

For structs:

```python
pose_schema = {
    "x_mm": "int32",
    "y_mm": "int32",
    "heading_mrad": "int16",
}

robot.define_message("my_pose", id=0x31, schema=pose_schema)
robot.publish("my_pose", {"x_mm": 1200, "y_mm": -400, "heading_mrad": 1570})
```

## Service Pattern

Services are request/response. Use them when the peer should answer a question.

```python
robot.define_service(
    "set_strategy",
    id=0x40,
    request="string",
    response="bool",
)

robot.serve("set_strategy", lambda requested: requested in ("attack", "defend"))

accepted = robot.call_service(
    "set_strategy",
    "attack",
    peer_id=2,
)
```

For an empty request:

```python
robot.define_service("battery_mv", id=0x41, request="empty", response="uint16")
robot.serve("battery_mv", lambda: battery.read_mv())

peer_battery_mv = robot.call_service("battery_mv", peer_id=2)
```

## Example Names For Every Type

The example script uses these topic and service names:

| Type | Topic | Service |
|------|-------|---------|
| `empty` | `datatype_empty_topic` | `datatype_empty_service` |
| `bool` | `datatype_bool_topic` | `datatype_bool_service` |
| `uint8` | `datatype_uint8_topic` | `datatype_uint8_service` |
| `int8` | `datatype_int8_topic` | `datatype_int8_service` |
| `uint16` | `datatype_uint16_topic` | `datatype_uint16_service` |
| `int16` | `datatype_int16_topic` | `datatype_int16_service` |
| `uint32` | `datatype_uint32_topic` | `datatype_uint32_service` |
| `int32` | `datatype_int32_topic` | `datatype_int32_service` |
| `uint64` | `datatype_uint64_topic` | `datatype_uint64_service` |
| `int64` | `datatype_int64_topic` | `datatype_int64_service` |
| `float32` | `datatype_float32_topic` | `datatype_float32_service` |
| `string` | `datatype_string_topic` | `datatype_string_service` |
| `bytes` | `datatype_bytes_topic` | `datatype_bytes_service` |
| `struct` | `datatype_struct_topic` | `datatype_struct_service` |

## C And C++ Payloads

The C/C++ wrapper does not use schema strings. It sends fixed-size binary
payloads. Use fixed-width types from `<stdint.h>` and mark custom structs with
`RJSCM_PACKED`.

Recommended field types:

| C/C++ type | Protocol meaning |
|------------|------------------|
| `uint8_t` | unsigned 8-bit integer or boolean encoded as `0`/`1` |
| `int8_t` | signed 8-bit integer |
| `uint16_t` | unsigned 16-bit integer |
| `int16_t` | signed 16-bit integer |
| `uint32_t` | unsigned 32-bit integer |
| `int32_t` | signed 32-bit integer |
| `uint64_t` | unsigned 64-bit integer |
| `int64_t` | signed 64-bit integer |
| `float` | 32-bit float |
| fixed byte arrays | raw fixed-size bytes |

Keep both robots on the same struct layout and byte order. Raspberry Pi and
ESP32 are little-endian, which matches the current intended setup.
