#!/usr/bin/env python3
"""Examples for every high-level rjscm custom payload datatype.

This example uses an in-memory transport so it can run on any development
machine without two physical modules. The same define/publish/serve/call code is
used with `rjscm.connect("/dev/ttyRC0", robot_id=...)` on a Raspberry Pi.
"""

from __future__ import annotations

import argparse
import math
import queue
from dataclasses import dataclass
from typing import Any

import rjscm
from sibcp import MemoryTransport


@dataclass(frozen=True)
class DatatypeExample:
    name: str
    schema: Any
    value: Any
    description: str


DATATYPE_EXAMPLES: tuple[DatatypeExample, ...] = (
    DatatypeExample("empty", "empty", None, "event with no payload"),
    DatatypeExample("bool", "bool", True, "true or false"),
    DatatypeExample("uint8", "uint8", 250, "0 to 255"),
    DatatypeExample("int8", "int8", -12, "-128 to 127"),
    DatatypeExample("uint16", "uint16", 65000, "0 to 65535"),
    DatatypeExample("int16", "int16", -1200, "-32768 to 32767"),
    DatatypeExample("uint32", "uint32", 4_000_000_000, "0 to 4294967295"),
    DatatypeExample("int32", "int32", -123_456, "-2147483648 to 2147483647"),
    DatatypeExample("uint64", "uint64", 9_000_000_000_000, "0 to 2^64 - 1"),
    DatatypeExample("int64", "int64", -9_000_000_000_000, "-2^63 to 2^63 - 1"),
    DatatypeExample("float32", "float32", 3.25, "32-bit floating point number"),
    DatatypeExample("string", "string", "goal-left", "UTF-8 text"),
    DatatypeExample("bytes", "bytes", b"\x01\x02RC", "raw bytes"),
    DatatypeExample(
        "struct",
        [
            ("visible", "bool"),
            ("x_mm", "int32"),
            ("battery_mv", "uint16"),
            ("label", "string"),
        ],
        {
            "visible": True,
            "x_mm": -350,
            "battery_mv": 7400,
            "label": "ally",
        },
        "ordered fields packed into one payload",
    ),
)


def values_match(expected: Any, actual: Any) -> bool:
    if isinstance(expected, float):
        return math.isclose(expected, actual, rel_tol=0.0, abs_tol=0.0001)
    if isinstance(expected, dict):
        return expected.keys() == actual.keys() and all(
            values_match(expected[key], actual[key]) for key in expected
        )
    return expected == actual


def run_demo(*, verbose: bool = True) -> list[str]:
    left_transport, right_transport = MemoryTransport.pair()
    publisher = rjscm.Module.from_transport(left_transport, robot_id=1)
    responder = rjscm.Module.from_transport(right_transport, robot_id=2)
    completed: list[str] = []

    try:
        for index, example in enumerate(DATATYPE_EXAMPLES):
            topic_name = f"datatype_{example.name}_topic"
            service_name = f"datatype_{example.name}_service"
            topic_id = 0x20 + index
            service_id = 0x40 + index
            received: queue.Queue[Any] = queue.Queue()

            publisher.define_message(topic_name, id=topic_id, schema=example.schema)
            responder.define_message(topic_name, id=topic_id, schema=example.schema)
            responder.on_message(topic_name, received.put)
            publisher.publish(topic_name, example.value)
            topic_value = received.get(timeout=0.5)
            if not values_match(example.value, topic_value):
                raise AssertionError(
                    f"{example.name} topic mismatch: {topic_value!r} != {example.value!r}"
                )

            publisher.define_service(
                service_name,
                id=service_id,
                request=example.schema,
                response=example.schema,
            )
            responder.define_service(
                service_name,
                id=service_id,
                request=example.schema,
                response=example.schema,
            )
            responder.serve(service_name, lambda request: request, advertise=False)
            responder.advertise_once()
            service_value = publisher.call_service(
                service_name,
                example.value,
                peer_id=2,
                timeout=0.5,
                discovery_timeout=0.5,
            )
            if not values_match(example.value, service_value):
                raise AssertionError(
                    f"{example.name} service mismatch: {service_value!r} != {example.value!r}"
                )

            completed.append(example.name)
            if verbose:
                print(f"PASS {example.name}: topic publish and service call")
    finally:
        responder.close()
        publisher.close()

    return completed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run topic and service examples for every rjscm datatype."
    )
    parser.add_argument("--quiet", action="store_true", help="only print the final summary")
    args = parser.parse_args()

    completed = run_demo(verbose=not args.quiet)
    print(f"PASS: {len(completed)} datatype topic/service examples")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
