import importlib.util
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from rjscm.schema import codec_from_schema
from sibcp import CodecError


EXAMPLE_PATH = os.path.join(
    os.path.dirname(__file__),
    "..",
    "examples",
    "all_datatypes_example.py",
)


def load_example_module():
    spec = importlib.util.spec_from_file_location("all_datatypes_example", EXAMPLE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RjscmDatatypeTests(unittest.TestCase):
    def test_all_datatype_topic_and_service_examples(self):
        module = load_example_module()

        completed = module.run_demo(verbose=False)

        self.assertEqual(
            completed,
            [
                "empty",
                "bool",
                "uint8",
                "int8",
                "uint16",
                "int16",
                "uint32",
                "int32",
                "uint64",
                "int64",
                "float32",
                "string",
                "bytes",
                "struct",
            ],
        )

    def test_documented_type_aliases_encode(self):
        cases = [
            ("empty", None),
            ("none", None),
            ("bool", True),
            ("boolean", False),
            ("uint8", 1),
            ("u8", 2),
            ("byte", 3),
            ("int8", -1),
            ("i8", -2),
            ("uint16", 100),
            ("u16", 101),
            ("int16", -100),
            ("i16", -101),
            ("uint32", 1000),
            ("u32", 1001),
            ("int32", -1000),
            ("i32", -1001),
            ("int", -1002),
            ("uint64", 10000),
            ("u64", 10001),
            ("int64", -10000),
            ("i64", -10001),
            ("float", 1.25),
            ("float32", 2.5),
            ("string", "hello"),
            ("str", "world"),
            ("bytes", b"abc"),
        ]

        for schema, value in cases:
            with self.subTest(schema=schema):
                codec = codec_from_schema(schema)
                encoded = codec.encode(value)
                decoded = codec.decode(encoded)
                if isinstance(value, float):
                    self.assertAlmostEqual(decoded, value, places=5)
                else:
                    self.assertEqual(decoded, value)

    def test_integer_ranges_are_enforced(self):
        with self.assertRaises(CodecError):
            codec_from_schema("uint8").encode(256)

        with self.assertRaises(CodecError):
            codec_from_schema("int8").encode(-129)
