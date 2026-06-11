import math
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from sibcp.codecs import (
    Bool,
    Bytes,
    CodecError,
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

INTEGER_BOUNDS = [
    (UInt8, 0, 255),
    (Int8, -128, 127),
    (UInt16, 0, 65535),
    (Int16, -32768, 32767),
    (UInt32, 0, 2**32 - 1),
    (Int32, -(2**31), 2**31 - 1),
    (UInt64, 0, 2**64 - 1),
    (Int64, -(2**63), 2**63 - 1),
]


class NumericCodecTests(unittest.TestCase):
    def test_integer_min_max_roundtrip(self):
        for codec, minimum, maximum in INTEGER_BOUNDS:
            for value in (minimum, maximum):
                with self.subTest(codec=repr(codec), value=value):
                    self.assertEqual(codec.decode(codec.encode(value)), value)

    def test_integer_out_of_range_rejected(self):
        for codec, minimum, maximum in INTEGER_BOUNDS:
            for value in (minimum - 1, maximum + 1):
                with self.subTest(codec=repr(codec), value=value):
                    with self.assertRaises(CodecError):
                        codec.encode(value)

    def test_float32_roundtrip(self):
        self.assertEqual(Float32.decode(Float32.encode(0.5)), 0.5)
        self.assertTrue(math.isinf(Float32.decode(Float32.encode(math.inf))))

    def test_short_payload_rejected(self):
        with self.assertRaises(CodecError):
            UInt32.decode(b"\x01\x02")


class BoolCodecTests(unittest.TestCase):
    def test_roundtrip(self):
        self.assertIs(Bool.decode(Bool.encode(True)), True)
        self.assertIs(Bool.decode(Bool.encode(False)), False)

    def test_rejects_non_bool_values(self):
        with self.assertRaises(CodecError):
            Bool.encode(2)
        with self.assertRaises(CodecError):
            Bool.encode("true")

    def test_rejects_invalid_wire_byte(self):
        with self.assertRaises(CodecError):
            Bool.decode(b"\x02")


class StringCodecTests(unittest.TestCase):
    def test_empty_string_roundtrip(self):
        self.assertEqual(String.encode(""), b"\x00\x00")
        self.assertEqual(String.decode(b"\x00\x00"), "")

    def test_unicode_roundtrip(self):
        value = "góól ⚽"
        self.assertEqual(String.decode(String.encode(value)), value)

    def test_rejects_invalid_utf8(self):
        with self.assertRaises(CodecError):
            String.decode(b"\x02\x00\xff\xfe")

    def test_rejects_truncated_payload(self):
        with self.assertRaises(CodecError):
            String.decode(b"\x05\x00abc")
        with self.assertRaises(CodecError):
            String.decode(b"\x05")

    def test_rejects_oversized_string(self):
        with self.assertRaises(CodecError):
            String.encode("x" * 65536)


class BytesCodecTests(unittest.TestCase):
    def test_empty_bytes_roundtrip(self):
        self.assertEqual(Bytes.decode(Bytes.encode(b"")), b"")

    def test_rejects_truncated_payload(self):
        with self.assertRaises(CodecError):
            Bytes.decode(b"\x04\x00ab")

    def test_rejects_non_bytes_value(self):
        with self.assertRaises(CodecError):
            Bytes.encode("text")


class EmptyCodecTests(unittest.TestCase):
    def test_roundtrip(self):
        self.assertEqual(Empty.encode(None), b"")
        self.assertIsNone(Empty.decode(b""))

    def test_rejects_value(self):
        with self.assertRaises(CodecError):
            Empty.encode(1)


class StructCodecTests(unittest.TestCase):
    def test_mixed_fields_roundtrip(self):
        codec = Struct(("id", UInt8), ("name", String), ("x", Int32))
        value = {"id": 7, "name": "blau", "x": -1200}

        self.assertEqual(codec.decode(codec.encode(value)), value)
        self.assertIsNone(codec.size)

    def test_fixed_size_struct_reports_size(self):
        codec = Struct(("a", UInt16), ("b", Int8))
        self.assertEqual(codec.size, 3)

    def test_missing_field_rejected(self):
        codec = Struct(("a", UInt8), ("b", UInt8))
        with self.assertRaises(CodecError):
            codec.encode({"a": 1})

    def test_trailing_bytes_rejected(self):
        codec = Struct(("a", UInt8),)
        with self.assertRaises(CodecError):
            codec.decode(b"\x01\x02")

    def test_duplicate_field_name_rejected(self):
        with self.assertRaises(CodecError):
            Struct(("a", UInt8), ("a", UInt8))

    def test_empty_struct_rejected(self):
        with self.assertRaises(CodecError):
            Struct()


if __name__ == "__main__":
    unittest.main()
