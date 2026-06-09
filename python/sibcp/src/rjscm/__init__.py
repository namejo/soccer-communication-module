"""High-level Python API for RoboCupJunior Soccer Communication Modules.

Most robot programs should start here:

    import rjscm
    robot = rjscm.connect("/dev/ttyACM0", robot_id=1)
"""

from sibcp import (
    GameState,
    LedMode,
    MelodyId,
    ServiceCallError,
    ServiceTimeoutError,
    TacticalRole,
)

from .module import (
    DEFAULT_BAUDRATE,
    DEFAULT_RESPONSE_TIMEOUT,
    Message,
    Module,
    PeriodicPublisher,
    Service,
    connect,
)
from .schema import codec_from_schema

__all__ = [
    "DEFAULT_BAUDRATE",
    "DEFAULT_RESPONSE_TIMEOUT",
    "GameState",
    "LedMode",
    "MelodyId",
    "Message",
    "Module",
    "PeriodicPublisher",
    "Service",
    "ServiceCallError",
    "ServiceTimeoutError",
    "TacticalRole",
    "codec_from_schema",
    "connect",
]
