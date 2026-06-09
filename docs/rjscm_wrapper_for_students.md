# RJSCM Python Wrapper For Students

`rjscm` is the easy Python API for the RoboCupJunior Soccer Communication
Module. Your robot program talks to the local module over USB-C serial. The
module forwards the message to the other module over Wi-Fi/ESP-NOW.

```text
your Python code -> USB-C serial -> your module -> wireless -> peer module -> peer Python code
```

The wireless bridge is currently best-effort broadcast. It is good for tactics
and sensor sharing, but it is not encrypted, authenticated, or guaranteed to
deliver every packet.

## Install

Run this once from the repository root:

```sh
python -m pip install -e python/sibcp[serial]
```

Then your robot code can import:

```python
import rjscm
```

## Connect Over USB-C

On a Raspberry Pi, the USB-C serial port is usually `/dev/ttyACM0`:

```python
import rjscm

robot = rjscm.connect("/dev/ttyACM0", robot_id=1)
```

Use a different `robot_id` on the second robot:

```python
robot = rjscm.connect("/dev/ttyACM0", robot_id=2)
```

You can also use a context manager so the serial port closes automatically:

```python
import rjscm

with rjscm.connect("/dev/ttyACM0", robot_id=1) as robot:
    robot.publish_ball(visible=True, x_mm=500, y_mm=0)
```

## Built-In Service: Does The Other Robot See The Ball?

Robot 2 answers the question:

```python
import rjscm

with rjscm.connect("/dev/ttyACM0", robot_id=2) as robot:
    robot.answer_see_ball(lambda: camera.sees_ball())

    while True:
        # Keep your normal robot loop here.
        pass
```

Robot 1 asks Robot 2:

```python
import rjscm

with rjscm.connect("/dev/ttyACM0", robot_id=1) as robot:
    if robot.ask_peer_sees_ball(peer_id=2):
        print("Robot 2 sees the ball")
    else:
        print("Robot 2 does not see the ball")
```

Services are advertised by the robot that answers them. The caller waits for
that advertisement before sending the request.

## Send A Custom Message

A message is one-way. Use it for facts like "my current location" or "I see an
opponent here".

Both robots must define the same message name, ID, and schema:

```python
robot.define_message(
    "my_location",
    id=0x30,
    schema={
        "x_mm": "int32",
        "y_mm": "int32",
        "heading_mrad": "int16",
    },
)
```

The sending robot publishes:

```python
robot.publish(
    "my_location",
    {
        "x_mm": 1200,
        "y_mm": -400,
        "heading_mrad": 1570,
    },
)
```

The receiving robot listens:

```python
def handle_location(message):
    print("Peer x:", message["x_mm"])
    print("Peer y:", message["y_mm"])

robot.on_message("my_location", handle_location)
```

## Publish A Topic At A Fixed Rate

Use this for sensor values that should be streamed all the time. The publishing
robot does not know whether another robot reads the value.

Sender:

```python
robot.expose_topic(
    "front_distance",
    id=0x32,
    schema={"distance_mm": "int32"},
    provider=lambda: {"distance_mm": distance_sensor.read_mm()},
    hz=20,
)
```

Receiver:

```python
def handle_distance(message):
    print("Peer front distance:", message["distance_mm"])

robot.subscribe("front_distance", handle_distance)
```

Topics are different from services:

- Topic: "Here is my newest sensor value."
- Service: "Please answer this question now."

## Create A Custom Service

A service is a question with an answer. Use it for things like "can you shoot?"
or "which role are you playing?".

Both robots define the same service:

```python
robot.define_service("can_shoot", id=0x31, response=bool)
```

The robot that answers exposes the service:

```python
robot.serve("can_shoot", lambda: kicker.is_ready())
```

The other robot calls it:

```python
peer_can_shoot = robot.call_service("can_shoot", peer_id=2)
```

## Payload Types

For the full list with sizes, ranges, aliases, and a runnable example, see
[RJSCM datatypes](rjscm_datatypes.md).

You can use Python types:

| Python | Protocol type |
|--------|---------------|
| `bool` | boolean |
| `int` | signed 32-bit integer |
| `float` | 32-bit float |
| `str` | UTF-8 string |
| `bytes` | bytes |

You can also use explicit string types:

| String | Meaning |
|--------|---------|
| `"bool"` | boolean |
| `"uint8"` | unsigned 8-bit integer |
| `"int8"` | signed 8-bit integer |
| `"uint16"` | unsigned 16-bit integer |
| `"int16"` | signed 16-bit integer |
| `"uint32"` | unsigned 32-bit integer |
| `"int32"` | signed 32-bit integer |
| `"uint64"` | unsigned 64-bit integer |
| `"int64"` | signed 64-bit integer |
| `"float32"` | 32-bit float |
| `"string"` | UTF-8 string |
| `"bytes"` | bytes |

## IDs

Every message and every service needs an ID. The ID is the small number that is
sent over the wire instead of the long name.

Rules:

- Both robots must use the same ID for the same message or service.
- Do not reuse the same message ID for two different messages.
- Do not reuse the same service ID for two different services.
- Use IDs from `0x20` to `0xEF` for your own team code.
- IDs `0xF0` to `0xFF` are reserved for system messages and services.

See [SIBCP ID allocation](sibcp_id_allocation.md) for the shared table.

## Useful Built-In Helpers

```python
robot.publish_ball(visible=True, x_mm=300, y_mm=-100)
robot.publish_pose(x_mm=1000, y_mm=200, heading_mrad=0)

robot.answer_see_ball(lambda: camera.sees_ball())
sees_ball = robot.ask_peer_sees_ball(peer_id=2)

robot.answer_role(rjscm.TacticalRole.DEFENDER)
peer_role = robot.ask_peer_role(peer_id=2)

robot.on_game_state(lambda state, robot_play: print(state, robot_play))
robot.set_led(red=0, green=128, blue=0)
robot.play_melody(rjscm.MelodyId.GOAL)
```

## Common Mistakes

- If `call_service` times out, check that the other robot called `serve(...)`.
- If a message is decoded incorrectly, check that both robots use the same
  schema and ID.
- If the serial port fails to open, check the device path with:

```sh
ls /dev/ttyACM*
```

- If Linux blocks the serial port, add your user to the `dialout` group or run
  the program with the needed permissions.

More setup help:

- [Raspberry Pi USB-C setup](raspberry_pi_setup.md)
- [RJSCM datatypes](rjscm_datatypes.md)
- [Two-module hardware checklist](two_module_hardware_checklist.md)
