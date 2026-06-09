# PR Draft: USB-C And Wi-Fi Module Communication

## Summary

This PR adds an experimental SIBCP-based robot-to-robot communication path for
the ESP32-C5 communication module.

Main changes:

- Exposes SIBCP frames over USB-C and UART1 for Raspberry Pi or robot-controller
  integration.
- Bridges validated SIBCP frames between local serial transports and ESP-NOW on
  2.4 GHz channel 1.
- Replaces plain-text `PLAY` / `STOP` serial output with structured
  `/system/game_state` topic frames.
- Adds reserved system topics for score, match time, and referee events.
- Adds local-only system services for status LED and buzzer melody control.
- Adds Python CLI helpers for beginner testing:
  - `sibcp service serve see_ball ...`
  - `sibcp service call see_ball ...`
- Adds high-level robot APIs:
  - Python: `import rjscm`
  - C/C++: `#include "rjscm.h"`
- Adds Linux udev helper script for stable `/dev/ttyRC0` and `/dev/ttyRC1`
  symlinks.
- Adds student documentation, wrapper examples, and tests.

## Why This Matters

Teams can connect a Raspberry Pi to the module's USB-C port and use the same
serial link for:

- Referee/game-state information from the module.
- Simple service calls between two robots.
- Topic-style sensor or world-model data between two robots.

This makes cooperation experiments easier without requiring students to write
ESP-NOW or BLE code themselves.

## How To Test

Build firmware:

```sh
cmake --build firmware/RCj_comm_module/build_wifi_features -j 4
```

Run Python tests:

```sh
python3 -m unittest discover -s python/sibcp/tests
```

Build and test the C/C++ wrapper:

```sh
cmake -S c/rjscm -B /tmp/rjscm-build
cmake --build /tmp/rjscm-build
ctest --test-dir /tmp/rjscm-build --output-on-failure
```

Manual two-module test:

```sh
python3 python/sibcp/examples/two_module_wifi_test.py \
  --left /dev/ttyACM0 \
  --right /dev/ttyACM1 \
  --timeout 2.0 \
  --settle 0.5 \
  --service-attempts 3 \
  --topic-attempts 3
```

High-level Python wrapper hardware test:

```sh
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/rjscm_two_module_check.py \
  --left /dev/ttyACM0 \
  --right /dev/ttyACM1 \
  --timeout 3
```

C/C++ wrapper hardware test:

```sh
/tmp/rjscm-build/see_ball_responder /dev/ttyACM1 2 true
/tmp/rjscm-build/see_ball_caller /dev/ttyACM0 1 2 2000
```

Datatype example test:

```sh
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/all_datatypes_example.py
```

udev stable-name dry run:

```sh
scripts/setup_rc_udev.sh list
scripts/setup_rc_udev.sh install --dry-run
```

Beginner service test:

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value true
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

## Known Limitations

- ESP-NOW is currently broadcast, unauthenticated, and unencrypted.
- Topic delivery is best-effort; there are no ACKs or retries yet.
- Service calls have timeouts and retries in the host tools, but the firmware
  bridge itself does not guarantee radio delivery.
- More than two modules on the same channel is not fully defined yet.
- BLE referee commands still need a stronger pairing/authentication design
  before adversarial match use.
- Hardware validation is still needed for:
  - UART1 pins IO4/IO5 with a robot controller.
  - Two separate Raspberry Pis.
  - BLE referee app connected while ESP-NOW communication is active.

## Documentation Added

- `docs/service_cli_for_students.md`
- `docs/rjscm_wrapper_for_students.md`
- `docs/rjscm_c_cpp_wrapper_for_students.md`
- `docs/rjscm_datatypes.md`
- `docs/two_module_hardware_checklist.md`
- `docs/pr_wifi_module_communication.md`
- `scripts/setup_rc_udev.sh`
- `SECURITY.md`
- `audit.md`
- `todo.md`
