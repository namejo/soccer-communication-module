"""Standard SIBCP schemas for common robot-to-robot messages."""

from __future__ import annotations

from enum import IntEnum

from .codecs import Bool, Empty, Int16, Int32, Struct, UInt8, UInt32

ROBOT_POSE_TOPIC = "/robot/pose"
ROBOT_POSE_TOPIC_ID = 0x10
WORLD_BALL_TOPIC = "/world/ball"
WORLD_BALL_TOPIC_ID = 0x11
WORLD_OPPONENT_TOPIC = "/world/opponent"
WORLD_OPPONENT_TOPIC_ID = 0x12
BALL_IN_VISION_SERVICE = "/ball_in_your_vision"
BALL_IN_VISION_SERVICE_ID = 0x01
REQUEST_ROLE_SERVICE = "/request_role"
REQUEST_ROLE_SERVICE_ID = 0x10


class TacticalRole(IntEnum):
    UNKNOWN = 0x00
    ATTACKER = 0x01
    DEFENDER = 0x02
    GOALIE_SUPPORT = 0x03
    SEARCHING = 0x04


RobotPose = Struct(
    ("x_mm", Int32),
    ("y_mm", Int32),
    ("heading_mrad", Int16),
    ("confidence", UInt8),
    ("timestamp_ms", UInt32),
)
WorldBall = Struct(
    ("visible", Bool),
    ("x_mm", Int32),
    ("y_mm", Int32),
    ("confidence", UInt8),
    ("timestamp_ms", UInt32),
)
WorldOpponent = Struct(
    ("visible", Bool),
    ("x_mm", Int32),
    ("y_mm", Int32),
    ("threat", UInt8),
    ("timestamp_ms", UInt32),
)
RoleResponse = Struct(("role", UInt8))


def define_robot_world_topics(node):
    """Register standard robot/world topics on a SibcpNode."""

    node.topic(ROBOT_POSE_TOPIC, topic_id=ROBOT_POSE_TOPIC_ID, payload=RobotPose)
    node.topic(WORLD_BALL_TOPIC, topic_id=WORLD_BALL_TOPIC_ID, payload=WorldBall)
    node.topic(
        WORLD_OPPONENT_TOPIC,
        topic_id=WORLD_OPPONENT_TOPIC_ID,
        payload=WorldOpponent,
    )


def define_robot_world_services(node):
    """Register standard robot-to-robot helper services on a SibcpNode."""

    node.service(
        BALL_IN_VISION_SERVICE,
        service_id=BALL_IN_VISION_SERVICE_ID,
        request=Empty,
        response=Bool,
    )
    node.service(
        REQUEST_ROLE_SERVICE,
        service_id=REQUEST_ROLE_SERVICE_ID,
        request=Empty,
        response=RoleResponse,
    )
