"""Reserved system topics emitted by the communication module firmware."""

from __future__ import annotations

from enum import IntEnum

from .codecs import Bool, Empty, Struct, UInt8, UInt16, UInt32

SYSTEM_GAME_STATE_TOPIC = "/system/game_state"
SYSTEM_GAME_STATE_TOPIC_ID = 0xF0
SYSTEM_SCORE_TOPIC = "/system/score"
SYSTEM_SCORE_TOPIC_ID = 0xF1
SYSTEM_MATCH_TIME_TOPIC = "/system/match_time"
SYSTEM_MATCH_TIME_TOPIC_ID = 0xF2
SYSTEM_REFEREE_EVENT_TOPIC = "/system/referee_event"
SYSTEM_REFEREE_EVENT_TOPIC_ID = 0xF3
SYSTEM_SET_LED_SERVICE = "/system/set_led"
SYSTEM_SET_LED_SERVICE_ID = 0xF0
SYSTEM_PLAY_MELODY_SERVICE = "/system/play_melody"
SYSTEM_PLAY_MELODY_SERVICE_ID = 0xF1


class GameState(IntEnum):
    INIT = 0x00
    DISCONNECTED = 0x01
    PLAY = 0x02
    STOP = 0x03
    DAMAGE = 0x04
    HALF_TIME = 0x05
    GAME_OVER = 0x06


class RefereeEvent(IntEnum):
    GOAL_OWN = 0x01
    GOAL_OPPONENT = 0x02
    PENALTY_STARTED = 0x03
    PENALTY_ENDED = 0x04
    HALF_STARTED = 0x05
    MATCH_ENDED = 0x06


class LedMode(IntEnum):
    OFF = 0x00
    SOLID = 0x01
    BLINK = 0x02
    PULSE = 0x03


class MelodyId(IntEnum):
    GOAL = 0x01
    ACK = 0x02


SystemGameState = Struct(("state", UInt8), ("robot_play", Bool))
SystemScore = Struct(("own_score", UInt8), ("opponent_score", UInt8))
SystemMatchTime = Struct(
    ("half", UInt8),
    ("remaining_ms", UInt32),
    ("phase_total_ms", UInt32),
)
SystemRefereeEvent = Struct(("event", UInt8))
SystemSetLedRequest = Struct(
    ("mode", UInt8),
    ("red", UInt8),
    ("green", UInt8),
    ("blue", UInt8),
    ("duration_ms", UInt16),
)
SystemPlayMelodyRequest = Struct(("melody_id", UInt8), ("repeat", UInt8))


def define_system_topics(node):
    """Register firmware-reserved system topics on a SibcpNode."""

    node.topic(
        SYSTEM_GAME_STATE_TOPIC,
        topic_id=SYSTEM_GAME_STATE_TOPIC_ID,
        payload=SystemGameState,
    )
    node.topic(SYSTEM_SCORE_TOPIC, topic_id=SYSTEM_SCORE_TOPIC_ID, payload=SystemScore)
    node.topic(
        SYSTEM_MATCH_TIME_TOPIC,
        topic_id=SYSTEM_MATCH_TIME_TOPIC_ID,
        payload=SystemMatchTime,
    )
    node.topic(
        SYSTEM_REFEREE_EVENT_TOPIC,
        topic_id=SYSTEM_REFEREE_EVENT_TOPIC_ID,
        payload=SystemRefereeEvent,
    )


def define_system_services(node):
    """Register firmware-owned local system services on a SibcpNode."""

    node.service(
        SYSTEM_SET_LED_SERVICE,
        service_id=SYSTEM_SET_LED_SERVICE_ID,
        request=SystemSetLedRequest,
        response=Empty,
    )
    node.service(
        SYSTEM_PLAY_MELODY_SERVICE,
        service_id=SYSTEM_PLAY_MELODY_SERVICE_ID,
        request=SystemPlayMelodyRequest,
        response=Empty,
    )
