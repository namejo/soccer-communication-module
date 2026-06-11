#!/usr/bin/env python3
"""Full two-module hardware validation over the ESP-NOW bridge.

Connect two modules over USB-C and run this script to exercise every message
and every service the stack offers:

- built-in topics (`ball`, `robot_pose`, `opponent`) in both directions
- built-in services (`see_ball`, `request_role`) in both directions
- a custom topic and a custom service for every payload datatype
- periodic topic publishing at a fixed rate
- local system services (`play_melody`, `set_led`)
- firmware service-discovery advertisements
- passive listening for referee system topics

Checks are independent: a failing check is reported and the rest still run.

Usage:
    python two_module_full_validation.py [--left /dev/ttyACM0] [--right /dev/ttyACM1]
"""

from __future__ import annotations

import argparse
import math
import queue
import time
from typing import Any, Callable

import rjscm
from rjscm import ServiceCallError, ServiceTimeoutError

DATATYPE_CASES: tuple[tuple[str, Any, Any], ...] = (
    ("empty", "empty", None),
    ("bool", "bool", True),
    ("uint8", "uint8", 250),
    ("int8", "int8", -12),
    ("uint16", "uint16", 65000),
    ("int16", "int16", -1200),
    ("uint32", "uint32", 4_000_000_000),
    ("int32", "int32", -123_456),
    ("uint64", "uint64", 9_000_000_000_000),
    ("int64", "int64", -9_000_000_000_000),
    ("float32", "float32", 3.25),
    ("string", "string", "góól ⚽"),
    ("bytes", "bytes", b"\x01\x02RC"),
    (
        "struct",
        [
            ("visible", "bool"),
            ("x_mm", "int32"),
            ("battery_mv", "uint16"),
            ("label", "string"),
        ],
        {"visible": True, "x_mm": -350, "battery_mv": 7400, "label": "ally"},
    ),
)

MODULE_ROBOT_ID = 0  # reserved id used by the firmware's own advertisements
MODULE_SERVICE_IDS = {0xF0, 0xF1}  # /system/set_led, /system/play_melody


class Report:
    def __init__(self) -> None:
        self.results: list[tuple[str, str, str]] = []

    def run(self, name: str, check: Callable[[], str | None]) -> None:
        try:
            detail = check() or ""
            self.results.append(("PASS", name, detail))
            print(f"PASS  {name}{f' — {detail}' if detail else ''}")
        except Exception as exc:
            self.results.append(("FAIL", name, str(exc)))
            print(f"FAIL  {name} — {exc}")

    def info(self, name: str, detail: str) -> None:
        self.results.append(("INFO", name, detail))
        print(f"INFO  {name} — {detail}")

    def skip(self, name: str, detail: str) -> None:
        self.results.append(("SKIP", name, detail))
        print(f"SKIP  {name} — {detail}")

    def summary(self) -> int:
        passed = sum(1 for status, _, _ in self.results if status == "PASS")
        failed = sum(1 for status, _, _ in self.results if status == "FAIL")
        print("\n" + "=" * 60)
        print(f"{passed} passed, {failed} failed, "
              f"{len(self.results) - passed - failed} informational/skipped")
        if failed:
            print("\nFailed checks:")
            for status, name, detail in self.results:
                if status == "FAIL":
                    print(f"  - {name}: {detail}")
        return 1 if failed else 0


def values_match(expected: Any, actual: Any) -> bool:
    if isinstance(expected, float):
        return math.isclose(expected, actual, rel_tol=0.0, abs_tol=0.0001)
    if isinstance(expected, dict):
        return expected.keys() == actual.keys() and all(
            values_match(expected[key], actual[key]) for key in expected
        )
    return expected == actual


def expect_topic(
    subscriber_queue: "queue.Queue[Any]",
    publish: Callable[[], None],
    expected: Any,
    timeout: float,
    attempts: int = 3,
) -> str:
    """Publish until the message arrives.

    Topics are fire-and-forget over broadcast ESP-NOW, so individual frames can
    be lost. Like robot code at a real match, the check resends; the attempt
    count is reported so persistent loss stays visible.
    """
    for attempt in range(1, attempts + 1):
        publish()
        try:
            message = subscriber_queue.get(timeout=timeout / attempts)
        except queue.Empty:
            continue
        if not values_match(expected, message):
            raise AssertionError(f"received {message!r}, expected {expected!r}")
        return "" if attempt == 1 else f"delivered on publish attempt {attempt}"
    raise AssertionError(f"no message after {attempts} publishes (radio frame loss)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--left", default="/dev/ttyACM0", help="left module serial port")
    parser.add_argument("--right", default="/dev/ttyACM1", help="right module serial port")
    parser.add_argument("--timeout", type=float, default=3.0, help="per-check timeout in seconds")
    args = parser.parse_args()
    timeout = args.timeout

    report = Report()
    left = rjscm.connect(args.left, robot_id=1)
    right = rjscm.connect(args.right, robot_id=2)

    system_topic_log: dict[str, list[Any]] = {
        "game_state": [], "score": [], "match_time": [], "referee_event": [],
    }
    for name, log in system_topic_log.items():
        left.on_message(name, log.append)

    try:
        # --- Built-in topics, both directions -------------------------------
        ball_at_right: queue.Queue[Any] = queue.Queue()
        pose_at_right: queue.Queue[Any] = queue.Queue()
        opponent_at_right: queue.Queue[Any] = queue.Queue()
        ball_at_left: queue.Queue[Any] = queue.Queue()
        right.on_message("ball", ball_at_right.put)
        right.on_message("robot_pose", pose_at_right.put)
        right.on_message("opponent", opponent_at_right.put)
        left.on_message("ball", ball_at_left.put)

        report.run("topic /world/ball left->right", lambda: expect_topic(
            ball_at_right,
            lambda: left.publish_ball(
                visible=True, x_mm=321, y_mm=-123, confidence=88, timestamp_ms=123456
            ),
            {"visible": True, "x_mm": 321, "y_mm": -123,
             "confidence": 88, "timestamp_ms": 123456},
            timeout,
        ))
        report.run("topic /world/ball right->left", lambda: expect_topic(
            ball_at_left,
            lambda: right.publish_ball(
                visible=False, x_mm=-1, y_mm=2, confidence=10, timestamp_ms=99
            ),
            {"visible": False, "x_mm": -1, "y_mm": 2,
             "confidence": 10, "timestamp_ms": 99},
            timeout,
        ))
        report.run("topic /robot/pose left->right", lambda: expect_topic(
            pose_at_right,
            lambda: left.publish_pose(
                x_mm=100, y_mm=-200, heading_mrad=1571, confidence=77, timestamp_ms=42
            ),
            {"x_mm": 100, "y_mm": -200, "heading_mrad": 1571,
             "confidence": 77, "timestamp_ms": 42},
            timeout,
        ))
        report.run("topic /world/opponent left->right", lambda: expect_topic(
            opponent_at_right,
            lambda: left.publish("opponent", {
                "visible": True, "x_mm": 555, "y_mm": -42,
                "threat": 3, "timestamp_ms": 1000,
            }),
            {"visible": True, "x_mm": 555, "y_mm": -42,
             "threat": 3, "timestamp_ms": 1000},
            timeout,
        ))

        # --- Built-in services, both directions -----------------------------
        left.answer_see_ball(False)
        right.answer_see_ball(True)
        left.answer_role(rjscm.TacticalRole.ATTACKER)
        right.answer_role(rjscm.TacticalRole.DEFENDER)

        def builtin_services_ready() -> None:
            left.wait_for_service("see_ball", peer_id=2, timeout=timeout)
            right.wait_for_service("see_ball", peer_id=1, timeout=timeout)

        report.run("service discovery robot<->robot", builtin_services_ready)

        report.run("service see_ball left->right", lambda: (
            None if left.ask_peer_sees_ball(peer_id=2, timeout=timeout) is True
            else (_ for _ in ()).throw(AssertionError("expected True"))
        ))
        report.run("service see_ball right->left", lambda: (
            None if right.ask_peer_sees_ball(peer_id=1, timeout=timeout) is False
            else (_ for _ in ()).throw(AssertionError("expected False"))
        ))
        report.run("service request_role left->right", lambda: (
            None
            if left.ask_peer_role(peer_id=2, timeout=timeout) == rjscm.TacticalRole.DEFENDER
            else (_ for _ in ()).throw(AssertionError("expected DEFENDER"))
        ))
        report.run("service request_role right->left", lambda: (
            None
            if right.ask_peer_role(peer_id=1, timeout=timeout) == rjscm.TacticalRole.ATTACKER
            else (_ for _ in ()).throw(AssertionError("expected ATTACKER"))
        ))

        # --- Every datatype as a custom topic and a custom service ----------
        for index, (name, schema, value) in enumerate(DATATYPE_CASES):
            topic_name = f"datatype_{name}_topic"
            service_name = f"datatype_{name}_service"
            topic_id = 0x20 + index
            service_id = 0x40 + index
            received: queue.Queue[Any] = queue.Queue()

            left.define_message(topic_name, id=topic_id, schema=schema)
            right.define_message(topic_name, id=topic_id, schema=schema)
            right.on_message(topic_name, received.put)
            left.define_service(service_name, id=service_id, request=schema, response=schema)
            right.define_service(service_name, id=service_id, request=schema, response=schema)
            right.serve(service_name, lambda request: request)

            def check_topic(topic=topic_name, val=value, q=received) -> str:
                return expect_topic(q, lambda: left.publish(topic, val), val, timeout)

            def check_service(service=service_name, val=value) -> None:
                actual = left.call_service(
                    service, val, peer_id=2, timeout=timeout, discovery_timeout=timeout
                )
                if not values_match(val, actual):
                    raise AssertionError(f"received {actual!r}, expected {val!r}")

            report.run(f"custom topic datatype {name}", check_topic)
            report.run(f"custom service datatype {name}", check_service)

        # --- Periodic publishing at a fixed rate ----------------------------
        def check_periodic() -> str:
            drained = 0
            while not ball_at_right.empty():
                ball_at_right.get_nowait()
                drained += 1
            publisher = left.publish_at_hz(
                "ball",
                lambda: {"visible": True, "x_mm": 1, "y_mm": 2,
                         "confidence": 50, "timestamp_ms": 0},
                hz=10.0,
            )
            try:
                time.sleep(1.2)
            finally:
                publisher.stop()
            count = ball_at_right.qsize()
            if count < 6:
                raise AssertionError(f"only {count} of ~12 periodic messages arrived")
            return f"{count} messages in 1.2 s at 10 Hz"

        report.run("periodic publishing 10 Hz left->right", check_periodic)

        # --- Local system services -------------------------------------------
        report.run("system service play_melody GOAL (left module)",
                   lambda: left.play_melody(rjscm.MelodyId.GOAL, timeout=timeout))
        report.run("system service play_melody ACK x2 (right module)",
                   lambda: right.play_melody(rjscm.MelodyId.ACK, repeat=2, timeout=timeout))

        def check_set_led(module: rjscm.Module, label: str) -> None:
            def attempt() -> str:
                try:
                    module.set_led(
                        red=0, green=0, blue=255,
                        mode=rjscm.LedMode.BLINK, duration_ms=2000, timeout=timeout,
                    )
                    return "accepted (module is in PLAY)"
                except ServiceCallError:
                    return ("rejected with ERROR — documented behavior while the referee "
                            "state is not PLAY (no referee app connected)")
            report.run(f"system service set_led ({label} module)", attempt)

        check_set_led(left, "left")
        check_set_led(right, "right")

        # --- Firmware service-discovery advertisement ------------------------
        def check_firmware_discovery(module: rjscm.Module, label: str) -> None:
            deadline = time.monotonic() + 2.0
            while time.monotonic() < deadline:
                advertised = module.node.discovered_services.get(MODULE_ROBOT_ID)
                if advertised and MODULE_SERVICE_IDS <= advertised:
                    report.run(
                        f"firmware discovery advertisement ({label} module)",
                        lambda: "module advertises /system/set_led and /system/play_melody",
                    )
                    return
                time.sleep(0.05)
            report.skip(
                f"firmware discovery advertisement ({label} module)",
                "no advertisement received — the flashed firmware predates the "
                "service-discovery feature; reflash with a build from this branch "
                "to enable it",
            )

        check_firmware_discovery(left, "left")
        check_firmware_discovery(right, "right")

        # --- Referee system topics (passive) ---------------------------------
        seen = {name: len(log) for name, log in system_topic_log.items() if log}
        if seen:
            report.info("referee system topics", f"received during this run: {seen}")
        else:
            report.info(
                "referee system topics",
                "none received (expected: /system/game_state, score, match_time and "
                "referee_event are only published when the referee app changes state "
                "over BLE — connect the app to exercise them)",
            )

    finally:
        left.close()
        right.close()

    return report.summary()


if __name__ == "__main__":
    raise SystemExit(main())
