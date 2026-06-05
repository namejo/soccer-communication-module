"""Reserved system topics emitted by the communication module firmware."""

from __future__ import annotations

from enum import IntEnum

from .codecs import Bool, Struct, UInt8

SYSTEM_GAME_STATE_TOPIC = "/system/game_state"
SYSTEM_GAME_STATE_TOPIC_ID = 0xF0


class GameState(IntEnum):
    INIT = 0x00
    DISCONNECTED = 0x01
    PLAY = 0x02
    STOP = 0x03
    DAMAGE = 0x04
    HALF_TIME = 0x05
    GAME_OVER = 0x06


SystemGameState = Struct(("state", UInt8), ("robot_play", Bool))


def define_system_topics(node):
    """Register firmware-reserved system topics on a SibcpNode."""

    node.topic(
        SYSTEM_GAME_STATE_TOPIC,
        topic_id=SYSTEM_GAME_STATE_TOPIC_ID,
        payload=SystemGameState,
    )
