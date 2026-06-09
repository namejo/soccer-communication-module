import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from sibcp import (
    BALL_IN_VISION_SERVICE,
    Bool,
    define_robot_world_services,
    define_robot_world_topics,
    define_system_services,
    GameState,
    LedMode,
    MelodyId,
    MemoryTransport,
    PacketType,
    RefereeEvent,
    REQUEST_ROLE_SERVICE,
    ServiceCallError,
    ServiceTimeoutError,
    SibcpNode,
    StreamingParser,
    SYSTEM_GAME_STATE_TOPIC,
    SYSTEM_MATCH_TIME_TOPIC,
    SYSTEM_PLAY_MELODY_SERVICE,
    SYSTEM_REFEREE_EVENT_TOPIC,
    SYSTEM_SCORE_TOPIC,
    SYSTEM_SET_LED_SERVICE,
    TacticalRole,
    WORLD_BALL_TOPIC,
    define_system_topics,
    encode_frame,
)


BALL_SERVICE = "/ball_in_your_vision"


class NodeTests(unittest.TestCase):
    def test_bool_service_call(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = SibcpNode(caller_transport)
        responder = SibcpNode(responder_transport)
        caller.service(BALL_SERVICE, service_id=1, response=Bool)
        responder.service(BALL_SERVICE, service_id=1, response=Bool)

        @responder.on_service(BALL_SERVICE)
        def handle_ball_request(_request):
            return True

        responder.start_background_reader()
        try:
            self.assertTrue(caller.call(BALL_SERVICE, timeout=0.5))
        finally:
            responder.close()
            caller.close()

    def test_bool_topic_publish(self):
        publisher_transport, subscriber_transport = MemoryTransport.pair()
        publisher = SibcpNode(publisher_transport)
        subscriber = SibcpNode(subscriber_transport)
        publisher.topic("/i_see_the_ball", topic_id=2, payload=Bool)
        subscriber.topic("/i_see_the_ball", topic_id=2, payload=Bool)
        received = []

        @subscriber.on_topic("/i_see_the_ball")
        def handle_topic(value):
            received.append(value)

        subscriber.start_background_reader()
        try:
            publisher.publish("/i_see_the_ball", True)
            deadline = time.monotonic() + 0.5
            while not received and time.monotonic() < deadline:
                time.sleep(0.005)
            self.assertEqual(received, [True])
        finally:
            subscriber.close()
            publisher.close()

    def test_system_game_state_topic(self):
        publisher_transport, subscriber_transport = MemoryTransport.pair()
        publisher = SibcpNode(publisher_transport)
        subscriber = SibcpNode(subscriber_transport)
        define_system_topics(publisher)
        define_system_topics(subscriber)
        received = []

        @subscriber.on_topic(SYSTEM_GAME_STATE_TOPIC)
        def handle_game_state(value):
            received.append(value)

        subscriber.start_background_reader()
        try:
            publisher.publish(
                SYSTEM_GAME_STATE_TOPIC,
                {"state": GameState.PLAY, "robot_play": True},
            )
            deadline = time.monotonic() + 0.5
            while not received and time.monotonic() < deadline:
                time.sleep(0.005)
            self.assertEqual(received, [{"state": GameState.PLAY, "robot_play": True}])
        finally:
            subscriber.close()
            publisher.close()

    def test_standard_robot_world_topic(self):
        publisher_transport, subscriber_transport = MemoryTransport.pair()
        publisher = SibcpNode(publisher_transport)
        subscriber = SibcpNode(subscriber_transport)
        define_robot_world_topics(publisher)
        define_robot_world_topics(subscriber)
        received = []

        @subscriber.on_topic(WORLD_BALL_TOPIC)
        def handle_ball(value):
            received.append(value)

        ball = {
            "visible": True,
            "x_mm": 1200,
            "y_mm": -250,
            "confidence": 88,
            "timestamp_ms": 123456,
        }

        subscriber.start_background_reader()
        try:
            publisher.publish(WORLD_BALL_TOPIC, ball)
            deadline = time.monotonic() + 0.5
            while not received and time.monotonic() < deadline:
                time.sleep(0.005)
            self.assertEqual(received, [ball])
        finally:
            subscriber.close()
            publisher.close()

    def test_standard_robot_world_services(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = SibcpNode(caller_transport)
        responder = SibcpNode(responder_transport)
        define_robot_world_services(caller)
        define_robot_world_services(responder)

        @responder.on_service(BALL_IN_VISION_SERVICE)
        def handle_ball_request(_request):
            return True

        @responder.on_service(REQUEST_ROLE_SERVICE)
        def handle_role_request(_request):
            return {"role": TacticalRole.DEFENDER}

        responder.start_background_reader()
        try:
            self.assertTrue(caller.call(BALL_IN_VISION_SERVICE, timeout=0.5))
            self.assertEqual(
                caller.call(REQUEST_ROLE_SERVICE, timeout=0.5),
                {"role": TacticalRole.DEFENDER},
            )
        finally:
            responder.close()
            caller.close()

    def test_system_score_time_and_referee_topics(self):
        publisher_transport, subscriber_transport = MemoryTransport.pair()
        publisher = SibcpNode(publisher_transport)
        subscriber = SibcpNode(subscriber_transport)
        define_system_topics(publisher)
        define_system_topics(subscriber)
        received = []

        @subscriber.on_topic(SYSTEM_SCORE_TOPIC)
        def handle_score(value):
            received.append(("score", value))

        @subscriber.on_topic(SYSTEM_MATCH_TIME_TOPIC)
        def handle_time(value):
            received.append(("time", value))

        @subscriber.on_topic(SYSTEM_REFEREE_EVENT_TOPIC)
        def handle_referee_event(value):
            received.append(("event", value))

        subscriber.start_background_reader()
        try:
            publisher.publish(SYSTEM_SCORE_TOPIC, {"own_score": 2, "opponent_score": 1})
            publisher.publish(
                SYSTEM_MATCH_TIME_TOPIC,
                {"half": 2, "remaining_ms": 45000, "phase_total_ms": 600000},
            )
            publisher.publish(
                SYSTEM_REFEREE_EVENT_TOPIC,
                {"event": RefereeEvent.GOAL_OWN},
            )
            deadline = time.monotonic() + 0.5
            while len(received) < 3 and time.monotonic() < deadline:
                time.sleep(0.005)
            self.assertEqual(
                received,
                [
                    ("score", {"own_score": 2, "opponent_score": 1}),
                    (
                        "time",
                        {
                            "half": 2,
                            "remaining_ms": 45000,
                            "phase_total_ms": 600000,
                        },
                    ),
                    ("event", {"event": RefereeEvent.GOAL_OWN}),
                ],
            )
        finally:
            subscriber.close()
            publisher.close()

    def test_system_services(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = SibcpNode(caller_transport)
        responder = SibcpNode(responder_transport)
        define_system_services(caller)
        define_system_services(responder)
        received = []

        @responder.on_service(SYSTEM_SET_LED_SERVICE)
        def handle_set_led(request):
            received.append(("led", request))

        @responder.on_service(SYSTEM_PLAY_MELODY_SERVICE)
        def handle_play_melody(request):
            received.append(("melody", request))

        responder.start_background_reader()
        try:
            self.assertIsNone(
                caller.call(
                    SYSTEM_SET_LED_SERVICE,
                    {
                        "mode": LedMode.BLINK,
                        "red": 0,
                        "green": 64,
                        "blue": 255,
                        "duration_ms": 500,
                    },
                    timeout=0.5,
                )
            )
            self.assertIsNone(
                caller.call(
                    SYSTEM_PLAY_MELODY_SERVICE,
                    {"melody_id": MelodyId.GOAL, "repeat": 2},
                    timeout=0.5,
                )
            )
            self.assertEqual(
                received,
                [
                    (
                        "led",
                        {
                            "mode": LedMode.BLINK,
                            "red": 0,
                            "green": 64,
                            "blue": 255,
                            "duration_ms": 500,
                        },
                    ),
                    ("melody", {"melody_id": MelodyId.GOAL, "repeat": 2}),
                ],
            )
        finally:
            responder.close()
            caller.close()

    def test_unsupported_service_returns_status_error(self):
        caller_transport, responder_transport = MemoryTransport.pair()
        caller = SibcpNode(caller_transport)
        responder = SibcpNode(responder_transport)
        caller.service(BALL_SERVICE, service_id=1, response=Bool)

        responder.start_background_reader()
        try:
            with self.assertRaises(ServiceCallError) as caught:
                caller.call(BALL_SERVICE, timeout=0.5)
            self.assertEqual(caught.exception.status_code, 2)
        finally:
            responder.close()
            caller.close()

    def test_call_honors_zero_timeout(self):
        caller_transport, _responder_transport = MemoryTransport.pair()
        caller = SibcpNode(caller_transport, response_timeout=10.0)
        caller.service(BALL_SERVICE, service_id=1, response=Bool)

        started = time.monotonic()
        try:
            with self.assertRaises(ServiceTimeoutError):
                caller.call(BALL_SERVICE, timeout=0.0)
            self.assertLess(time.monotonic() - started, 0.1)
        finally:
            caller.close()

    def test_service_response_must_match_service_id(self):
        caller_transport, peer_transport = MemoryTransport.pair()
        caller = SibcpNode(caller_transport)
        caller.service(BALL_SERVICE, service_id=1, response=Bool)
        results = []
        errors = []

        def call_service():
            try:
                results.append(caller.call(BALL_SERVICE, timeout=0.5))
            except BaseException as exc:  # pragma: no cover - assertion path
                errors.append(exc)

        thread = threading.Thread(target=call_service)
        thread.start()

        try:
            request_frame = self._read_peer_frame(peer_transport)
            wrong_service_response = encode_frame(
                PacketType.SERVICE_RESPONSE,
                request_frame.transaction_id,
                2,
                bytes([0, 1]),
            )
            peer_transport.write(wrong_service_response)
            time.sleep(0.05)
            self.assertTrue(thread.is_alive())

            correct_service_response = encode_frame(
                PacketType.SERVICE_RESPONSE,
                request_frame.transaction_id,
                1,
                bytes([0, 1]),
            )
            peer_transport.write(correct_service_response)
            thread.join(timeout=0.5)

            self.assertFalse(thread.is_alive())
            self.assertEqual(results, [True])
            self.assertEqual(errors, [])
        finally:
            caller.close()
            thread.join(timeout=0.1)

    def _read_peer_frame(self, transport):
        parser = StreamingParser()
        deadline = time.monotonic() + 0.5
        while time.monotonic() < deadline:
            data = transport.read(1)
            for byte in data:
                frame = parser.push_byte(byte)
                if frame is not None:
                    return frame
        self.fail("timed out waiting for peer frame")


if __name__ == "__main__":
    unittest.main()
