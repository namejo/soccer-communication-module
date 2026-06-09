# Service CLI for Students

This guide shows how to test one robot asking the other robot a question.

Think of it like this:

```text
Robot A asks: "Do you see the ball?"
Robot B answers: "yes" or "no"
```

The communication modules move the message:

```text
Robot A computer -> USB-C -> Module A -> Wi-Fi -> Module B -> USB-C -> Robot B computer
```

The module does not decide the answer. The program running on Robot B decides the
answer and sends it back.

## Install the Python Helper

From the repository root:

```sh
python -m pip install -e python/sibcp[serial]
```

After this, you can use the `sibcp` command. If the command is not on your PATH yet,
use this instead:

```sh
PYTHONPATH=python/sibcp/src python -m sibcp
```

## Example With Two Modules on One Raspberry Pi

If both modules are plugged into the same Raspberry Pi, Linux may show:

```text
/dev/ttyACM0
/dev/ttyACM1
```

One terminal will pretend to be Robot B. It exposes the `see_ball` service and answers
`true`.

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value true
```

A second terminal will pretend to be Robot A. It waits until Robot B advertises that
`see_ball` exists, then it calls the service.

```sh
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

Expected output:

```text
waiting for see_ball advertisement from robot 2...
found see_ball on robot 2
see_ball response: true
```

On the Robot B terminal you should also see:

```text
received see_ball request -> sending true
```

## Why It Waits First

The caller first listens for a service advertisement. That advertisement is a small
message that says:

```text
Robot 2 has service see_ball
```

Only after the caller sees that advertisement does it send the real request.

This helps catch common mistakes:

- The other module is not powered.
- The wrong serial port was selected.
- The responder program is not running.
- The responder is not exposing the service you want to call.

## Change the Answer

If Robot B does not see the ball:

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value false
```

Now Robot A should receive:

```text
see_ball response: false
```

## Use Two Raspberry Pis

If each robot has its own Raspberry Pi:

On Robot B:

```sh
sibcp service serve see_ball --port /dev/ttyACM0 --robot-id 2 --value true
```

On Robot A:

```sh
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

Each Raspberry Pi uses its own local USB-C module port. It is usually `/dev/ttyACM0`,
but check with:

```sh
ls /dev/ttyACM*
```

## Important Limit

The current Wi-Fi bridge uses ESP-NOW broadcast. Broadcast is fast and simple, but it
does not guarantee that every single message arrives. The command retries service calls,
but real robot code should still be written so one missed message does not break the game.

## Another Built-In Service

The CLI also has `request_role`.

Robot B serves a role:

```sh
sibcp service serve request_role --port /dev/ttyACM1 --robot-id 2 --value defender
```

Robot A asks for it:

```sh
sibcp service call request_role --port /dev/ttyACM0 --peer-id 2
```

Expected answer:

```text
request_role response: defender
```
