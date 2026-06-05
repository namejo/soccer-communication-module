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

## Boolean Service Example

Responder robot over the exposed UART pins:

```python
from sibcp import Bool, SerialTransport, SibcpNode

node = SibcpNode(SerialTransport("/dev/ttyAMA0", baudrate=460800))
node.service("/ball_in_your_vision", service_id=1, response=Bool)

@node.on_service("/ball_in_your_vision")
def handle_ball_request(_request):
    return camera.sees_ball()

node.start_background_reader()
```

Caller robot over USB-C:

```python
from sibcp import Bool, SerialTransport, SibcpNode

node = SibcpNode(SerialTransport("/dev/ttyACM0", baudrate=460800))
node.service("/ball_in_your_vision", service_id=1, response=Bool)

sees_ball = node.call("/ball_in_your_vision", timeout=0.2)
```

On Raspberry Pi, use `/dev/ttyAMA0` or another UART device for the exposed module
UART pins, and `/dev/ttyACM0` for USB-C. The USB-C baud rate is ignored by the
native USB serial device, but setting `460800` keeps examples consistent.

## System Game State

The firmware emits referee state changes as a reserved system topic instead of
plain `PLAY`/`STOP` text:

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
