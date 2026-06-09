# Two-Module Hardware Test Checklist

Use this checklist before presenting the Wi-Fi communication feature as ready
for other teams.

## Setup

- Flash the same firmware image to both ESP32-C5 modules.
- Connect both modules over USB-C to the Raspberry Pi or test laptop.
- Confirm both serial devices exist:

```sh
ls /dev/ttyACM*
```

Expected for a two-module local test:

```text
/dev/ttyACM0
/dev/ttyACM1
```

For stable device names on Linux, install physical-port udev symlinks:

```sh
scripts/setup_rc_udev.sh install
```

After installation, use `/dev/ttyRC0` and `/dev/ttyRC1` in the test commands if
those links exist.

Install the Python package:

```sh
python -m pip install -e python/sibcp[serial]
```

## Basic Service Test

Terminal 1:

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value true
```

Terminal 2:

```sh
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

Pass criteria:

- The caller discovers robot 2.
- The service call returns `true`.
- BLE logs show SIBCP forwarding or diagnostics without repeated send failures.

## Automated Local Two-Module Test

```sh
python3 python/sibcp/examples/two_module_wifi_test.py \
  --left /dev/ttyACM0 \
  --right /dev/ttyACM1 \
  --timeout 2.0 \
  --settle 0.5 \
  --service-attempts 3 \
  --topic-attempts 3
```

Pass criteria:

- Service discovery succeeds.
- Service call succeeds.
- Topic transfer succeeds.
- Local `/system/set_led` and `/system/play_melody` requests return valid
  service responses.

## Python Wrapper Test

Run the high-level wrapper hardware check:

```sh
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/rjscm_two_module_check.py \
  --left /dev/ttyACM0 \
  --right /dev/ttyACM1 \
  --timeout 3
```

Or run a small `rjscm` responder on one module and a caller on the other.

Responder:

```python
import time
import rjscm

with rjscm.connect("/dev/ttyACM1", robot_id=2) as robot:
    robot.answer_see_ball(lambda: True)
    while True:
        time.sleep(0.1)
```

Caller:

```python
import rjscm

with rjscm.connect("/dev/ttyACM0", robot_id=1) as robot:
    print(robot.ask_peer_sees_ball(peer_id=2))
```

Pass criteria:

- The caller prints `True`.
- Repeated calls do not time out under normal desk-test distance.

## C/C++ Wrapper Test

Build the wrapper and examples:

```sh
cmake -S c/rjscm -B /tmp/rjscm-build
cmake --build /tmp/rjscm-build
```

Run the C responder and C++ caller examples:

```sh
/tmp/rjscm-build/see_ball_responder /dev/ttyACM1 2 true
/tmp/rjscm-build/see_ball_caller /dev/ttyACM0 1 2 2000
```

Pass criteria:

- The C responder keeps serving `see_ball`.
- The C++ caller receives the expected response.
- No duplicate-ID or payload-size errors are reported.

## UART1 Pin Test

- Connect robot-controller TX to module `RX1`.
- Connect robot-controller RX to module `TX1`.
- Share GND.
- Use 3.3 V UART levels.
- Use 460800 baud, 8N1.

Pass criteria:

- UART1 receives `/system/game_state` frames.
- UART1 can send SIBCP topic/service frames that reach the peer module.
- USB-C and UART1 behavior are consistent.

## BLE Coexistence Test

- Connect the referee app over BLE.
- Keep the two-module Wi-Fi service test running.
- Send PLAY and STOP from the app.

Pass criteria:

- The service test still succeeds.
- `/system/game_state` frames appear on USB-C.
- The buzzer and LED state still match the referee state.

## Packet-Loss Soak Test

Run the automated two-module test repeatedly or with higher topic attempt counts
for at least 10 minutes.

Pass criteria:

- No firmware crash or serial disconnect.
- Send-failure/drop counters in BLE diagnostic logs do not grow continuously at
  short range.
- Any observed loss is documented in the PR notes.
