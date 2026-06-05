import os
import sys
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from sibcp import (
    Bool,
    GameState,
    MemoryTransport,
    ServiceCallError,
    ServiceTimeoutError,
    SibcpNode,
    SYSTEM_GAME_STATE_TOPIC,
    define_system_topics,
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


if __name__ == "__main__":
    unittest.main()
