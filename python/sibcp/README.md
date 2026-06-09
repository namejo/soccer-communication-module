# SIBCP Python Library

This package gives robot code a small Python API for the Smart Inter-Bot
Communication Protocol used by the communication module firmware.

The firmware is only a transport bridge:

```text
robot code -- serial/SIBCP -- local module -- ESP-NOW -- peer module -- serial/SIBCP -- peer robot code
```

The Python library owns the application-level meaning of paths such as
`/ball_in_your_vision`. Both robots must register the same path-to-ID mapping,
because SIBCP frames carry compact numeric identifiers instead of strings.

## Install

From the repository root:

```sh
python -m pip install -e python/sibcp[serial]
```

The core package has no runtime dependency. The `serial` extra installs
`pyserial` for real UART or USB serial ports.

## High-Level `rjscm` Wrapper

Team robot code should usually import `rjscm`. It keeps the serial protocol,
message IDs, service discovery, and payload encoding behind a smaller API:

```python
import rjscm

with rjscm.connect("/dev/ttyACM0", robot_id=1) as robot:
    robot.answer_see_ball(lambda: camera.sees_ball())

    if robot.ask_peer_sees_ball(peer_id=2):
        print("peer robot sees the ball")
```

Define custom team messages with Python types or simple strings:

```python
robot.define_message(
    "my_location",
    id=0x30,
    schema={"x_mm": "int32", "y_mm": "int32", "heading_mrad": "int16"},
)
robot.publish("my_location", {"x_mm": 1200, "y_mm": -400, "heading_mrad": 1570})
```

Publish a sensor topic at a fixed rate:

```python
robot.expose_topic(
    "front_distance",
    id=0x32,
    schema={"distance_mm": "int32"},
    provider=lambda: {"distance_mm": distance_sensor.read_mm()},
    hz=20,
)

peer.subscribe("front_distance", lambda msg: print(msg["distance_mm"]))
```

Define custom request/response services the same way:

```python
robot.define_service("can_shoot", id=0x31, response=bool)
robot.serve("can_shoot", lambda: kicker.is_ready())

peer_can_shoot = robot.call_service("can_shoot", peer_id=2)
```

Both robots must use the same name, ID, and schema. Use IDs `0x20` through
`0xEF` for team-defined messages and services to avoid the standard and system
IDs. For a student-oriented guide, see
[`docs/rjscm_wrapper_for_students.md`](../../docs/rjscm_wrapper_for_students.md).
For the full datatype list and topic/service examples for each type, see
[`docs/rjscm_datatypes.md`](../../docs/rjscm_datatypes.md).

## Standard Robot Services

### Quick command-line test

You can test a service without writing Python code.

On the module that should answer the question:

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value true
```

On the module that should ask the question:

```sh
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

The caller first waits until robot `2` advertises the `see_ball` service. Then it sends
the service request and prints the response.

For a beginner-friendly explanation, see
[`docs/service_cli_for_students.md`](../../docs/service_cli_for_students.md).

Responder robot over the exposed UART pins:

```python
from sibcp import (
    BALL_IN_VISION_SERVICE,
    SerialTransport,
    SibcpNode,
    define_robot_world_services,
)

node = SibcpNode(SerialTransport("/dev/ttyAMA0", baudrate=460800))
define_robot_world_services(node)

@node.on_service(BALL_IN_VISION_SERVICE)
def handle_ball_request(_request):
    return camera.sees_ball()

node.start_background_reader()
```

Caller robot over USB-C:

```python
from sibcp import BALL_IN_VISION_SERVICE, SerialTransport, SibcpNode, define_robot_world_services

node = SibcpNode(SerialTransport("/dev/ttyACM0", baudrate=460800))
define_robot_world_services(node)

sees_ball = node.call(BALL_IN_VISION_SERVICE, timeout=0.2)
```

On Raspberry Pi, use `/dev/ttyAMA0` or another UART device for the exposed module
UART pins, and `/dev/ttyACM0` for USB-C. The USB-C baud rate is ignored by the
native USB serial device, but setting `460800` keeps examples consistent.

## Standard World Topics

The helper also defines common robot/world payloads for location and perception:

```python
from time import monotonic

from sibcp import SerialTransport, SibcpNode, WORLD_BALL_TOPIC, define_robot_world_topics

node = SibcpNode(SerialTransport("/dev/ttyACM0", baudrate=460800))
define_robot_world_topics(node)

node.publish(
    WORLD_BALL_TOPIC,
    {
        "visible": True,
        "x_mm": 1200,
        "y_mm": -250,
        "confidence": 88,
        "timestamp_ms": int(monotonic() * 1000),
    },
)
```

Available helpers:

| Path | ID | Payload |
|------|----|---------|
| `/robot/pose` | `0x10` | `x_mm`, `y_mm`, `heading_mrad`, `confidence`, `timestamp_ms` |
| `/world/ball` | `0x11` | `visible`, `x_mm`, `y_mm`, `confidence`, `timestamp_ms` |
| `/world/opponent` | `0x12` | `visible`, `x_mm`, `y_mm`, `threat`, `timestamp_ms` |
| `/ball_in_your_vision` | service `0x01` | empty request, boolean response |
| `/request_role` | service `0x10` | empty request, `role` response |

## System Topics

The firmware emits referee/app data as reserved system topics instead of plain
serial text:

```python
from sibcp import (
    GameState,
    SerialTransport,
    SibcpNode,
    SYSTEM_GAME_STATE_TOPIC,
    define_system_topics,
)

node = SibcpNode(SerialTransport("/dev/ttyACM0", baudrate=460800))
define_system_topics(node)

@node.on_topic(SYSTEM_GAME_STATE_TOPIC)
def handle_game_state(message):
    state = GameState(message["state"])
    robot_can_move = message["robot_play"]
```

The payload is `{"state": uint8, "robot_play": bool}`. `robot_play` is the direct
replacement for the old `PLAY`/`STOP` output.

Additional firmware-owned topics:

| Path | ID | Payload |
|------|----|---------|
| `/system/score` | `0xF1` | `own_score`, `opponent_score` |
| `/system/match_time` | `0xF2` | `half`, `remaining_ms`, `phase_total_ms` |
| `/system/referee_event` | `0xF3` | `event` |

Referee events use `RefereeEvent`: `GOAL_OWN`, `GOAL_OPPONENT`,
`PENALTY_STARTED`, `PENALTY_ENDED`, `HALF_STARTED`, and `MATCH_ENDED`.

## Local System Services

Some services are consumed by the local module and are not broadcast to the peer:

```python
from sibcp import (
    LedMode,
    MelodyId,
    SerialTransport,
    SibcpNode,
    SYSTEM_PLAY_MELODY_SERVICE,
    SYSTEM_SET_LED_SERVICE,
    define_system_services,
)

node = SibcpNode(SerialTransport("/dev/ttyACM0", baudrate=460800))
define_system_services(node)

node.call(
    SYSTEM_SET_LED_SERVICE,
    {"mode": LedMode.BLINK, "red": 0, "green": 128, "blue": 255, "duration_ms": 500},
)
node.call(SYSTEM_PLAY_MELODY_SERVICE, {"melody_id": MelodyId.GOAL, "repeat": 2})
```

The firmware rejects `/system/set_led` unless the referee state is PLAY, so STOP red
safety feedback stays authoritative.

## Topics

Topics are one-way messages:

```python
from sibcp import Bool, SibcpNode

node.topic("/i_see_the_ball", topic_id=2, payload=Bool)
node.publish("/i_see_the_ball", True)

@node.on_topic("/i_see_the_ball")
def update_peer_state(value):
    peer_sees_ball = value
```

## Struct Payloads

Structs are packed by concatenating field codecs in order. Decoding returns a
plain dict:

```python
from sibcp import Int64, Struct

AddThreeRequest = Struct(("a", Int64), ("b", Int64), ("c", Int64))
AddThreeResponse = Struct(("sum", Int64))
```

Service response frames automatically add the leading status byte:

| Status | Meaning |
|--------|---------|
| `0` | success |
| `1` | handler error |
| `2` | unsupported service |

## Test

```sh
python -m unittest discover python/sibcp/tests
```
