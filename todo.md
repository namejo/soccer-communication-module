# TODO

Status for branch: `feature_wifi_module_communication`

Last updated: 2026-06-09

## Done

- [x] Add SIBCP frame support for robot-to-robot messages over the communication module.
- [x] Expose SIBCP over USB-C, so a Raspberry Pi can use `/dev/ttyACM*`.
- [x] Keep UART1 on IO4/IO5 as a robot-host transport at 460800 baud.
- [x] Replace plain text `PLAY` / `STOP` serial output with reserved `/system/game_state` topic frames.
- [x] Add firmware system topics:
  - `/system/game_state`
  - `/system/score`
  - `/system/match_time`
  - `/system/referee_event`
- [x] Add local firmware system services:
  - `/system/set_led`
  - `/system/play_melody`
- [x] Make `/system/set_led` local-only and reject it outside PLAY state.
- [x] Add buzzer support for V7 2026 / robofuze hardware on IO26.
- [x] Buzz when the game state changes.
- [x] Add predefined melody support for system melody commands.
- [x] Set status LED green in PLAY and red in STOP / stopped output states.
- [x] Reduce status LED brightness to 50%.
- [x] Add Python SIBCP package schemas for common robot data:
  - `/robot/pose`
  - `/world/ball`
  - `/world/opponent`
  - `/ball_in_your_vision`
  - `/request_role`
- [x] Add ESP-NOW bridge between modules.
- [x] Move ESP-NOW bridge to 2.4 GHz channel 1 after hardware testing showed 5 GHz channel 36 did not deliver peer frames.
- [x] Add two-module hardware integration test:
  - `python/sibcp/examples/two_module_wifi_test.py`
- [x] Verify two modules over USB-C:
  - `/dev/ttyACM0`
  - `/dev/ttyACM1`
- [x] Verify service discovery, service calls, topics, and local system services with decoded USB serial traces.
- [x] Add CLI command for beginner service testing:
  - `sibcp service serve see_ball ...`
  - `sibcp service call see_ball ...`
- [x] Make service caller wait for service advertisement before sending the request.
- [x] Add student-facing guide:
  - `docs/service_cli_for_students.md`
- [x] Add unit tests for the CLI helper.
- [x] Add high-level Python wrapper importable as `import rjscm`:
  - `python/sibcp/src/rjscm/__init__.py`
  - `python/sibcp/src/rjscm/module.py`
  - `python/sibcp/src/rjscm/schema.py`
- [x] Add Python helper API for:
  - USB-C connect with `rjscm.connect("/dev/ttyACM0", robot_id=...)`
  - custom messages
  - periodic topic publishers with a fixed rate in Hz
  - custom services
  - service discovery before calls
  - `answer_see_ball`
  - `ask_peer_sees_ball`
  - `publish_ball`
  - `publish_pose`
  - local LED and melody services
- [x] Add student-facing Python wrapper guide:
  - `docs/rjscm_wrapper_for_students.md`
- [x] Add unit tests for the Python `rjscm` wrapper:
  - `python/sibcp/tests/test_rjscm.py`
- [x] Add high-level C/C++ wrapper:
  - `c/rjscm/include/rjscm.h`
  - `c/rjscm/src/rjscm.c`
  - `c/rjscm/CMakeLists.txt`
- [x] Add C API for:
  - USB-C serial open
  - custom messages
  - periodic topic publishers with a fixed rate in Hz
  - custom services
  - service discovery before calls
  - `rjscm_answer_see_ball`
  - `rjscm_ask_peer_sees_ball`
  - `rjscm_publish_ball`
  - `rjscm_publish_pose`
  - local LED and melody services
- [x] Add C++ convenience wrapper in `rjscm.h`:
  - `rjscm::Module::open(...)`
  - `defineMessage<T>`
  - `publish<T>`
  - `publishAtHz<T>`
  - `onMessage<T>`
  - `serve<T>`
  - `callService<T>`
  - `answerSeeBall`
  - `askPeerSeesBall`
- [x] Add C/C++ examples:
  - `c/rjscm/examples/see_ball_responder.c`
  - `c/rjscm/examples/see_ball_caller.cpp`
- [x] Add student-facing C/C++ wrapper guide:
  - `docs/rjscm_c_cpp_wrapper_for_students.md`
- [x] Add C and C++ wrapper tests:
  - `c/rjscm/tests/test_rjscm.c`
  - `c/rjscm/tests/test_rjscm_cpp.cpp`
- [x] Fix Python service response matching so responses must match both transaction ID and service ID.
- [x] Add Python regression test for mismatched service response IDs.
- [x] Reject duplicate custom topic/service IDs in the C/C++ wrapper while keeping built-in aliases.
- [x] Add C regression coverage for duplicate custom IDs and standard service aliases.
- [x] Drop externally received reserved system topic/service frames in firmware.
- [x] Add host-side firmware protocol regression test:
  - `c/rjscm/tests/test_firmware_sibcp_protocol.cpp`
- [x] Add ESP-NOW diagnostics:
  - Init success/failure counters.
  - Send callback success/failure counters.
  - RX/TX/drop counters.
  - Periodic BLE diagnostic log output.
- [x] Add PR CI workflow:
  - Python editable install/import smoke.
  - Python unit tests.
  - C/C++ CMake tests.
  - CMake install smoke.
  - ESP32-C5 firmware build.
- [x] Add root README demo commands:
  - `sibcp service serve see_ball`
  - `sibcp service call see_ball`
- [x] Add root README wrapper section:
  - Python `import rjscm`
  - C/C++ `#include "rjscm.h"`
- [x] Add C/C++ wrapper install/export support:
  - `cmake --install`
  - `rjscm::rjscm` CMake target.
- [x] Add install/link examples for normal robot projects.
- [x] Make `RJSCM_PACKED` available for custom C payload structs and document it.
- [x] Document SIBCP ID allocation rules:
  - `docs/sibcp_id_allocation.md`
- [x] Add examples for custom student topics and services:
  - Python location topic and `can_shoot` service.
  - C/C++ packed location topic, periodic topic, and `can_shoot` service.
- [x] Add Raspberry Pi setup notes:
  - `docs/raspberry_pi_setup.md`
- [x] Add two-module hardware test checklist:
  - `docs/two_module_hardware_checklist.md`
- [x] Add PR description draft:
  - `docs/pr_wifi_module_communication.md`
- [x] Add release-note warning for ESP-NOW best-effort broadcast:
  - `docs/release_notes_wifi_communication.md`
- [x] Replace the informal `stm_init()` timer comment with a production comment.
- [x] Run Python tests successfully:
  - `python3 -m unittest discover -s python/sibcp/tests`
- [x] Run Python package install/import smoke successfully:
  - `python3 -m pip install -e "python/sibcp[serial]"`
  - `python3 -c "import sibcp, rjscm; print(sibcp.__name__, rjscm.__name__)"`
  - `sibcp --help`
- [x] Build and test C/C++ wrapper plus firmware protocol test successfully:
  - `cmake -S c/rjscm -B /tmp/rjscm-build`
  - `cmake --build /tmp/rjscm-build`
  - `ctest --test-dir /tmp/rjscm-build --output-on-failure`
  - `cmake --install /tmp/rjscm-build --prefix /tmp/rjscm-install`
- [x] Build firmware successfully:
  - `cmake --build build_wifi_features -j 4`
- [x] Flash and test both modules with the current Wi-Fi communication firmware.
- [x] Hardware-test the Python `rjscm` wrapper over USB-C with two real modules:
  - `PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/rjscm_two_module_check.py --left /dev/ttyACM0 --right /dev/ttyACM1 --timeout 3`
- [x] Make C/C++ examples buildable and configurable by serial port.
- [x] Hardware-test the C/C++ `rjscm.h` wrapper over USB-C with two real modules:
  - `/tmp/rjscm-build/see_ball_responder /dev/ttyACM1 2 true`
  - `/tmp/rjscm-build/see_ball_caller /dev/ttyACM0 1 2 2000`
- [x] Add Linux udev helper for stable `/dev/ttyRC0` and `/dev/ttyRC1` names:
  - `scripts/setup_rc_udev.sh`
- [x] Verify udev helper discovery and dry-run rule generation:
  - `scripts/setup_rc_udev.sh list`
  - `scripts/setup_rc_udev.sh install --dry-run`
- [x] Add datatype documentation:
  - `docs/rjscm_datatypes.md`
- [x] Add topic publisher and service examples for every Python `rjscm` datatype:
  - `python/sibcp/examples/all_datatypes_example.py`
- [x] Add datatype regression tests:
  - `python/sibcp/tests/test_rjscm_datatypes.py`
- [x] Document future base-station/commentary/audio-event listener idea:
  - `docs/ai/13_sibcp_future_ideas.md`

## Still Open

- [ ] Prepare the pull request:
  - Review final diff.
  - Stage only intended files.
  - Commit with a clear message.
  - Push branch to fork.
  - Open PR against upstream.
- [ ] Decide whether this branch should include only Wi-Fi communication, or also the earlier buzzer/status LED/system-topic changes.
- [ ] Test with two separate Raspberry Pis, not only one host with two USB-C modules.
- [ ] Test the exposed UART pins IO4/IO5 with a robot controller, not only USB-C.
- [ ] Test while the referee app is connected over BLE, because BLE and Wi-Fi run at the same time.
- [ ] Run a longer packet-loss test for ESP-NOW broadcast.
- [ ] Add real protocol reliability:
  - ACKs.
  - Retries.
  - Duplicate filtering.
  - Timeouts.
  - Sequence numbers.
- [ ] Add peer addressing instead of pure broadcast.
- [ ] Define what should happen when more than two modules are active.
- [ ] Add message authentication or encryption for Wi-Fi communication before match use.
- [ ] Decide how keys should be provisioned for Wi-Fi security.
- [ ] Decide whether service discovery should live only in robot code or also partly in firmware.
- [ ] Consider a friendlier command for custom boolean services, for example:
  - `sibcp service define my_service --id 20 --type bool`
- [ ] Decide whether the CLI should support JSON payloads for custom services/topics.

## Useful Test Commands

Install Python helper:

```sh
python -m pip install -e python/sibcp[serial]
```

Serve `see_ball` on one module:

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value true
```

Call `see_ball` from the other module:

```sh
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

Run the two-module hardware integration test:

```sh
python3 python/sibcp/examples/two_module_wifi_test.py --left /dev/ttyACM0 --right /dev/ttyACM1 --timeout 2.0 --settle 0.5 --service-attempts 3 --topic-attempts 3
```

Run the high-level Python wrapper hardware check:

```sh
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/rjscm_two_module_check.py --left /dev/ttyACM0 --right /dev/ttyACM1 --timeout 3
```

Run the datatype topic/service example:

```sh
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/all_datatypes_example.py
```

Preview stable `/dev/ttyRC*` udev rules:

```sh
scripts/setup_rc_udev.sh list
scripts/setup_rc_udev.sh install --dry-run
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

Run the C/C++ wrapper hardware check:

```sh
/tmp/rjscm-build/see_ball_responder /dev/ttyACM1 2 true
/tmp/rjscm-build/see_ball_caller /dev/ttyACM0 1 2 2000
```

Build firmware:

```sh
cmake --build firmware/RCj_comm_module/build_wifi_features -j 4
```
