import os
import sys
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import rjscm
from sibcp import MemoryTransport, ServiceTimeoutError


def wait_until(predicate, timeout=0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.005)
    return predicate()


class RjscmWrapperTests(unittest.TestCase):
    def test_import_and_custom_message(self):
        left_transport, right_transport = MemoryTransport.pair()
        left = rjscm.Module.from_transport(left_transport, robot_id=1)
        right = rjscm.Module.from_transport(right_transport, robot_id=2)
        received = []

        try:
            location_schema = {"x_mm": "int32", "y_mm": "int32"}
            left.define_message("my_location", id=0x30, schema=location_schema)
            right.define_message("my_location", id=0x30, schema=location_schema)

            right.on_message("my_location", received.append)
            left.publish("my_location", {"x_mm": 1200, "y_mm": -500})

            self.assertTrue(wait_until(lambda: bool(received)))
            self.assertEqual(received, [{"x_mm": 1200, "y_mm": -500}])
        finally:
            right.close()
            left.close()

    def test_custom_service_waits_for_advertisement(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = rjscm.Module.from_transport(caller_transport, robot_id=1)
        responder = rjscm.Module.from_transport(responder_transport, robot_id=2)

        try:
            caller.define_service("can_shoot", id=0x31, response=bool)
            responder.define_service("can_shoot", id=0x31, response=bool)
            responder.serve("can_shoot", lambda: True, advertise=False)
            responder.advertise_once()

            caller.wait_for_service("can_shoot", peer_id=2, timeout=0.5)
            self.assertTrue(caller.call_service("can_shoot", peer_id=2, timeout=0.5))
        finally:
            responder.close()
            caller.close()

    def test_unserved_service_is_not_advertised(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = rjscm.Module.from_transport(caller_transport, robot_id=1)
        responder = rjscm.Module.from_transport(responder_transport, robot_id=2)

        try:
            caller.define_service("can_shoot", id=0x31, response=bool)
            responder.define_service("can_shoot", id=0x31, response=bool)
            responder.advertise_once()

            with self.assertRaises(ServiceTimeoutError):
                caller.wait_for_service("can_shoot", peer_id=2, timeout=0.05)
        finally:
            responder.close()
            caller.close()

    def test_builtin_see_ball_helper(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = rjscm.Module.from_transport(caller_transport, robot_id=1)
        responder = rjscm.Module.from_transport(responder_transport, robot_id=2)

        try:
            responder.answer_see_ball(lambda: True, advertise=False)
            responder.advertise_once()

            self.assertTrue(
                caller.ask_peer_sees_ball(
                    peer_id=2,
                    timeout=0.5,
                    discovery_timeout=0.5,
                )
            )
        finally:
            responder.close()
            caller.close()

    def test_builtin_topic_helpers(self):
        left_transport, right_transport = MemoryTransport.pair()
        left = rjscm.Module.from_transport(left_transport, robot_id=1)
        right = rjscm.Module.from_transport(right_transport, robot_id=2)
        received = []

        try:
            right.on_message("ball", received.append)
            left.publish_ball(
                visible=True,
                x_mm=100,
                y_mm=-20,
                confidence=80,
                timestamp_ms=123,
            )

            self.assertTrue(wait_until(lambda: bool(received)))
            self.assertEqual(
                received,
                [
                    {
                        "visible": True,
                        "x_mm": 100,
                        "y_mm": -20,
                        "confidence": 80,
                        "timestamp_ms": 123,
                    }
                ],
            )
        finally:
            right.close()
            left.close()

    def test_periodic_topic_publisher(self):
        left_transport, right_transport = MemoryTransport.pair()
        left = rjscm.Module.from_transport(left_transport, robot_id=1)
        right = rjscm.Module.from_transport(right_transport, robot_id=2)
        received = []
        counter = {"value": 0}

        def provide_sensor_value():
            counter["value"] += 1
            return {"distance_mm": counter["value"]}

        try:
            schema = {"distance_mm": "int32"}
            left.define_message("front_distance", id=0x41, schema=schema)
            right.define_message("front_distance", id=0x41, schema=schema)
            right.subscribe("front_distance", received.append)

            publisher = left.publish_at_hz(
                "front_distance",
                provide_sensor_value,
                hz=50,
            )

            self.assertTrue(wait_until(lambda: len(received) >= 2, timeout=0.5))
            publisher.stop()
            self.assertGreaterEqual(len(received), 2)
            self.assertGreater(received[-1]["distance_mm"], received[0]["distance_mm"])
        finally:
            right.close()
            left.close()


if __name__ == "__main__":
    unittest.main()
