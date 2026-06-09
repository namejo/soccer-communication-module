#!/usr/bin/env python3
"""Check the high-level rjscm API with two real USB-C modules."""

from __future__ import annotations

import argparse
import queue

import rjscm


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a two-module hardware check through the high-level rjscm API."
    )
    parser.add_argument("--left", default="/dev/ttyACM0", help="left module serial port")
    parser.add_argument("--right", default="/dev/ttyACM1", help="right module serial port")
    parser.add_argument("--timeout", type=float, default=3.0, help="operation timeout in seconds")
    args = parser.parse_args()

    left = rjscm.connect(args.left, robot_id=1)
    right = rjscm.connect(args.right, robot_id=2)
    received_ball: queue.Queue[dict] = queue.Queue()

    try:
        left.answer_see_ball(False)
        right.answer_see_ball(True)
        left.answer_role(rjscm.TacticalRole.ATTACKER)
        right.answer_role(rjscm.TacticalRole.DEFENDER)
        right.on_message("ball", received_ball.put)

        print("Waiting for service discovery...")
        left.wait_for_service("see_ball", peer_id=2, timeout=args.timeout)
        right.wait_for_service("see_ball", peer_id=1, timeout=args.timeout)

        print("Calling high-level see_ball helpers...")
        left_sees_right_ball = left.ask_peer_sees_ball(peer_id=2, timeout=args.timeout)
        right_sees_left_ball = right.ask_peer_sees_ball(peer_id=1, timeout=args.timeout)
        assert left_sees_right_ball is True, left_sees_right_ball
        assert right_sees_left_ball is False, right_sees_left_ball

        print("Calling high-level role helpers...")
        assert left.ask_peer_role(peer_id=2, timeout=args.timeout) == rjscm.TacticalRole.DEFENDER
        assert right.ask_peer_role(peer_id=1, timeout=args.timeout) == rjscm.TacticalRole.ATTACKER

        print("Publishing high-level ball topic...")
        left.publish_ball(
            visible=True,
            x_mm=321,
            y_mm=-123,
            confidence=88,
            timestamp_ms=123456,
        )
        ball_message = received_ball.get(timeout=args.timeout)
        assert ball_message == {
            "visible": True,
            "x_mm": 321,
            "y_mm": -123,
            "confidence": 88,
            "timestamp_ms": 123456,
        }, ball_message

        print("Playing local system melody on both modules...")
        left.play_melody(timeout=args.timeout)
        right.play_melody(timeout=args.timeout)

        print("PASS: rjscm high-level USB-C hardware wrapper check")
        return 0
    finally:
        left.close()
        right.close()


if __name__ == "__main__":
    raise SystemExit(main())
