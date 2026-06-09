"""Hardware integration test for two SIBCP modules over USB-C and ESP-NOW.

The script opens two communication modules, acts as the robot host on both
ports, and verifies that robot/world messages cross the Wi-Fi link while
firmware-owned /system services are answered locally by each module.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import queue
import sys
import threading
import time
from typing import Any

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT / "src"))

from sibcp import (  # noqa: E402
    BALL_IN_VISION_SERVICE,
    LedMode,
    MelodyId,
    PacketType,
    REQUEST_ROLE_SERVICE,
    ROBOT_POSE_TOPIC,
    SerialTransport,
    ServiceCallError,
    ServiceTimeoutError,
    SibcpNode,
    SYSTEM_PLAY_MELODY_SERVICE,
    SYSTEM_SET_LED_SERVICE,
    TacticalRole,
    WORLD_BALL_TOPIC,
    WORLD_OPPONENT_TOPIC,
    StreamingParser,
    define_robot_world_services,
    define_robot_world_topics,
    define_system_services,
    define_system_topics,
)
from sibcp.protocol import packet_type_name  # noqa: E402


NAME_BY_ID = {
    PacketType.TOPIC: {
        0x10: ROBOT_POSE_TOPIC,
        0x11: WORLD_BALL_TOPIC,
        0x12: WORLD_OPPONENT_TOPIC,
        0xF0: "/system/game_state",
        0xF1: "/system/score",
        0xF2: "/system/match_time",
        0xF3: "/system/referee_event",
    },
    PacketType.SERVICE_REQUEST: {
        0x01: BALL_IN_VISION_SERVICE,
        0x10: REQUEST_ROLE_SERVICE,
        0xF0: SYSTEM_SET_LED_SERVICE,
        0xF1: SYSTEM_PLAY_MELODY_SERVICE,
    },
    PacketType.SERVICE_RESPONSE: {
        0x01: BALL_IN_VISION_SERVICE,
        0x10: REQUEST_ROLE_SERVICE,
        0xF0: SYSTEM_SET_LED_SERVICE,
        0xF1: SYSTEM_PLAY_MELODY_SERVICE,
    },
    PacketType.SERVICE_DISCOVERY: {0x00: "/service_discovery"},
}


@dataclass(frozen=True)
class TraceEvent:
    timestamp: float
    port_name: str
    direction: str
    packet_type: int
    transaction_id: int
    identifier_id: int
    payload: bytes

    def format(self) -> str:
        packet = packet_type_name(self.packet_type)
        try:
            packet_type = PacketType(self.packet_type)
        except ValueError:
            packet_type = None
        path = NAME_BY_ID.get(packet_type, {}).get(self.identifier_id, f"id:{self.identifier_id}")
        payload = self.payload.hex() or "-"
        return (
            f"{self.timestamp:9.3f}s {self.port_name:>4} {self.direction:<2} "
            f"{packet:<17} {path:<24} tx={self.transaction_id:<5} "
            f"len={len(self.payload):<3} payload={payload}"
        )


class TraceRecorder:
    def __init__(self) -> None:
        self._started_at = time.monotonic()
        self._events: list[TraceEvent] = []
        self._lock = threading.Lock()

    def mark(self) -> int:
        with self._lock:
            return len(self._events)

    def add(self, port_name: str, direction: str, frame: Any) -> None:
        event = TraceEvent(
            timestamp=time.monotonic() - self._started_at,
            port_name=port_name,
            direction=direction,
            packet_type=frame.packet_type,
            transaction_id=frame.transaction_id,
            identifier_id=frame.identifier_id,
            payload=frame.payload,
        )
        with self._lock:
            self._events.append(event)

    def events_since(self, mark: int) -> list[TraceEvent]:
        with self._lock:
            return list(self._events[mark:])

    def all_events(self) -> list[TraceEvent]:
        with self._lock:
            return list(self._events)


class TracingTransport:
    def __init__(self, port_name: str, inner: SerialTransport, recorder: TraceRecorder):
        self.port_name = port_name
        self.inner = inner
        self.recorder = recorder
        self._rx_parser = StreamingParser()

    def read(self, size: int = 1) -> bytes:
        data = self.inner.read(size)
        if data:
            for frame in self._rx_parser.feed(data):
                self.recorder.add(self.port_name, "RX", frame)
        return data

    def write(self, data: bytes | bytearray | memoryview) -> int:
        parser = StreamingParser()
        for frame in parser.feed(bytes(data)):
            self.recorder.add(self.port_name, "TX", frame)
        return self.inner.write(data)

    def close(self) -> None:
        self.inner.close()


def define_all_messages(node: SibcpNode) -> None:
    define_robot_world_topics(node)
    define_robot_world_services(node)
    define_system_topics(node)
    define_system_services(node)


def reset_serial_buffers(transport: SerialTransport) -> None:
    serial = getattr(transport, "serial", None)
    if serial is None:
        return
    serial.reset_input_buffer()
    serial.reset_output_buffer()


def wait_for_value(values: "queue.Queue[Any]", timeout: float) -> Any:
    return values.get(timeout=timeout)


def publish_and_check_topic(
    results: list[bool],
    label: str,
    publisher: SibcpNode,
    path: str,
    values: "queue.Queue[Any]",
    expected: Any,
    timeout: float,
    attempts: int,
) -> None:
    attempts = max(1, attempts)
    per_attempt_timeout = max(0.05, timeout / attempts)

    for attempt in range(1, attempts + 1):
        publisher.publish(path, expected)
        try:
            actual = wait_for_value(values, per_attempt_timeout)
        except queue.Empty:
            continue

        check(
            results,
            label,
            actual == expected,
            f"actual={actual!r} expected={expected!r} attempts={attempt}",
        )
        return

    check(results, label, False, f"timed out after {attempts} publish attempt(s)")


def check(results: list[bool], label: str, condition: bool, detail: str = "") -> None:
    status = "PASS" if condition else "FAIL"
    suffix = f" - {detail}" if detail else ""
    print(f"{status}: {label}{suffix}")
    results.append(condition)


def has_frame(
    events: list[TraceEvent],
    *,
    port_name: str | None = None,
    direction: str | None = None,
    packet_type: PacketType | None = None,
    identifier_id: int | None = None,
) -> bool:
    for event in events:
        if port_name is not None and event.port_name != port_name:
            continue
        if direction is not None and event.direction != direction:
            continue
        if packet_type is not None and event.packet_type != packet_type:
            continue
        if identifier_id is not None and event.identifier_id != identifier_id:
            continue
        return True
    return False


def call_peer_service(
    caller: SibcpNode,
    caller_name: str,
    responder_name: str,
    path: str,
    expected: Any,
    timeout: float,
    attempts: int,
    recorder: TraceRecorder,
    results: list[bool],
) -> None:
    attempts = max(1, attempts)
    last_error: BaseException | None = None

    for attempt in range(1, attempts + 1):
        mark = recorder.mark()
        try:
            actual = caller.call(path, timeout=timeout)
        except ServiceTimeoutError as exc:
            last_error = exc
            continue
        break
    else:
        check(
            results,
            f"{caller_name}->{responder_name} {path}",
            False,
            f"{last_error} after {attempts} attempt(s)",
        )
        return

    events = recorder.events_since(mark)
    check(
        results,
        f"{caller_name}->{responder_name} {path} response",
        actual == expected,
        f"actual={actual!r} expected={expected!r} attempts={attempt}",
    )
    check(
        results,
        f"{caller_name} wrote request on USB",
        has_frame(
            events,
            port_name=caller_name,
            direction="TX",
            packet_type=PacketType.SERVICE_REQUEST,
        ),
    )
    check(
        results,
        f"{responder_name} received request on USB",
        has_frame(
            events,
            port_name=responder_name,
            direction="RX",
            packet_type=PacketType.SERVICE_REQUEST,
        ),
    )
    check(
        results,
        f"{caller_name} received response on USB",
        has_frame(
            events,
            port_name=caller_name,
            direction="RX",
            packet_type=PacketType.SERVICE_RESPONSE,
        ),
    )


def call_local_system_service(
    node: SibcpNode,
    local_name: str,
    peer_name: str,
    path: str,
    request: dict[str, int],
    timeout: float,
    recorder: TraceRecorder,
    results: list[bool],
    *,
    accept_error: bool = False,
) -> None:
    mark = recorder.mark()
    try:
        node.call(path, request, timeout=timeout)
        call_ok = True
        detail = "status=OK"
    except ServiceCallError as exc:
        call_ok = accept_error and exc.status_code == 1
        detail = f"status=ERROR({exc.status_code})"
    except ServiceTimeoutError as exc:
        check(results, f"{local_name} local {path}", False, str(exc))
        return

    events = recorder.events_since(mark)
    check(results, f"{local_name} local {path}", call_ok, detail)
    check(
        results,
        f"{local_name} wrote local system request on USB",
        has_frame(
            events,
            port_name=local_name,
            direction="TX",
            packet_type=PacketType.SERVICE_REQUEST,
        ),
    )
    check(
        results,
        f"{local_name} received local system response on USB",
        has_frame(
            events,
            port_name=local_name,
            direction="RX",
            packet_type=PacketType.SERVICE_RESPONSE,
        ),
    )
    check(
        results,
        f"{peer_name} did not receive local {path}",
        not has_frame(
            events,
            port_name=peer_name,
            direction="RX",
            packet_type=PacketType.SERVICE_REQUEST,
        ),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", default="/dev/ttyACM0")
    parser.add_argument("--right", default="/dev/ttyACM1")
    parser.add_argument("--baudrate", type=int, default=460800)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--service-attempts", type=int, default=3)
    parser.add_argument("--topic-attempts", type=int, default=3)
    args = parser.parse_args()

    recorder = TraceRecorder()
    left_serial = SerialTransport(args.left, baudrate=args.baudrate, timeout=0.01)
    right_serial = SerialTransport(args.right, baudrate=args.baudrate, timeout=0.01)
    left: SibcpNode | None = None
    right: SibcpNode | None = None

    try:
        time.sleep(args.settle)
        reset_serial_buffers(left_serial)
        reset_serial_buffers(right_serial)

        left = SibcpNode(
            TracingTransport("ACM0", left_serial, recorder),
            response_timeout=args.timeout,
            logger=lambda message: print(f"ACM0 node: {message}"),
        )
        right = SibcpNode(
            TracingTransport("ACM1", right_serial, recorder),
            response_timeout=args.timeout,
            logger=lambda message: print(f"ACM1 node: {message}"),
        )
        define_all_messages(left)
        define_all_messages(right)

        left_ball: "queue.Queue[Any]" = queue.Queue()
        left_pose: "queue.Queue[Any]" = queue.Queue()
        left_opponent: "queue.Queue[Any]" = queue.Queue()
        right_ball: "queue.Queue[Any]" = queue.Queue()
        right_pose: "queue.Queue[Any]" = queue.Queue()
        right_opponent: "queue.Queue[Any]" = queue.Queue()

        left.on_topic(WORLD_BALL_TOPIC, left_ball.put)
        left.on_topic(ROBOT_POSE_TOPIC, left_pose.put)
        left.on_topic(WORLD_OPPONENT_TOPIC, left_opponent.put)
        right.on_topic(WORLD_BALL_TOPIC, right_ball.put)
        right.on_topic(ROBOT_POSE_TOPIC, right_pose.put)
        right.on_topic(WORLD_OPPONENT_TOPIC, right_opponent.put)

        left.on_service(BALL_IN_VISION_SERVICE, lambda _request: False)
        right.on_service(BALL_IN_VISION_SERVICE, lambda _request: True)
        left.on_service(
            REQUEST_ROLE_SERVICE,
            lambda _request: {"role": int(TacticalRole.ATTACKER)},
        )
        right.on_service(
            REQUEST_ROLE_SERVICE,
            lambda _request: {"role": int(TacticalRole.DEFENDER)},
        )

        left.start_background_reader()
        right.start_background_reader()
        time.sleep(0.3)

        results: list[bool] = []

        left.advertise_services(source_robot_id=1)
        right.advertise_services(source_robot_id=2)
        time.sleep(0.3)
        check(
            results,
            "ACM1 discovered ACM0 service list",
            1 in right.discovered_services
            and 0x01 in right.discovered_services[1]
            and 0x10 in right.discovered_services[1],
            f"discovered={right.discovered_services}",
        )
        check(
            results,
            "ACM0 discovered ACM1 service list",
            2 in left.discovered_services
            and 0x01 in left.discovered_services[2]
            and 0x10 in left.discovered_services[2],
            f"discovered={left.discovered_services}",
        )

        call_peer_service(
            left,
            "ACM0",
            "ACM1",
            BALL_IN_VISION_SERVICE,
            True,
            args.timeout,
            args.service_attempts,
            recorder,
            results,
        )
        call_peer_service(
            right,
            "ACM1",
            "ACM0",
            BALL_IN_VISION_SERVICE,
            False,
            args.timeout,
            args.service_attempts,
            recorder,
            results,
        )
        call_peer_service(
            left,
            "ACM0",
            "ACM1",
            REQUEST_ROLE_SERVICE,
            {"role": int(TacticalRole.DEFENDER)},
            args.timeout,
            args.service_attempts,
            recorder,
            results,
        )
        call_peer_service(
            right,
            "ACM1",
            "ACM0",
            REQUEST_ROLE_SERVICE,
            {"role": int(TacticalRole.ATTACKER)},
            args.timeout,
            args.service_attempts,
            recorder,
            results,
        )

        expected_ball = {
            "visible": True,
            "x_mm": 420,
            "y_mm": -180,
            "confidence": 91,
            "timestamp_ms": 1000,
        }
        publish_and_check_topic(
            results,
            "ACM0->ACM1 world ball topic",
            left,
            WORLD_BALL_TOPIC,
            right_ball,
            expected_ball,
            args.timeout,
            args.topic_attempts,
        )

        expected_pose = {
            "x_mm": -120,
            "y_mm": 700,
            "heading_mrad": 1570,
            "confidence": 88,
            "timestamp_ms": 2000,
        }
        publish_and_check_topic(
            results,
            "ACM1->ACM0 robot pose topic",
            right,
            ROBOT_POSE_TOPIC,
            left_pose,
            expected_pose,
            args.timeout,
            args.topic_attempts,
        )

        expected_opponent = {
            "visible": True,
            "x_mm": 950,
            "y_mm": 120,
            "threat": 7,
            "timestamp_ms": 3000,
        }
        publish_and_check_topic(
            results,
            "ACM1->ACM0 opponent topic",
            right,
            WORLD_OPPONENT_TOPIC,
            left_opponent,
            expected_opponent,
            args.timeout,
            args.topic_attempts,
        )

        call_local_system_service(
            left,
            "ACM0",
            "ACM1",
            SYSTEM_PLAY_MELODY_SERVICE,
            {"melody_id": int(MelodyId.ACK), "repeat": 1},
            args.timeout,
            recorder,
            results,
        )
        call_local_system_service(
            right,
            "ACM1",
            "ACM0",
            SYSTEM_PLAY_MELODY_SERVICE,
            {"melody_id": int(MelodyId.ACK), "repeat": 1},
            args.timeout,
            recorder,
            results,
        )
        call_local_system_service(
            left,
            "ACM0",
            "ACM1",
            SYSTEM_SET_LED_SERVICE,
            {
                "mode": int(LedMode.SOLID),
                "red": 0,
                "green": 0,
                "blue": 64,
                "duration_ms": 200,
            },
            args.timeout,
            recorder,
            results,
            accept_error=True,
        )

        print("\nDecoded USB serial trace:")
        for event in recorder.all_events():
            print(event.format())

        return 0 if all(results) else 1
    finally:
        if left is not None:
            left.close()
        if right is not None:
            right.close()
        if left is None:
            left_serial.close()
        if right is None:
            right_serial.close()


if __name__ == "__main__":
    raise SystemExit(main())
