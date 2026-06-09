# Codebase Audit

Status for branch/worktree: `feature_wifi_module_communication`

Review date: 2026-06-09

Scope: current working tree, including modified and untracked firmware, Python,
C/C++ wrapper, docs, and test files.

Verification performed during this audit:

```sh
python3 -m pip install -e "python/sibcp[serial]"
python3 -c "import sibcp, rjscm; print(sibcp.__name__, rjscm.__name__)"
sibcp --help
python3 -m unittest discover -s python/sibcp/tests
cmake --build /tmp/rjscm-build
ctest --test-dir /tmp/rjscm-build --output-on-failure
cmake --install /tmp/rjscm-build --prefix /tmp/rjscm-install
cmake --build firmware/RCj_comm_module/build_wifi_features -j 4
python3 python/sibcp/examples/two_module_wifi_test.py --left /dev/ttyACM0 --right /dev/ttyACM1 --timeout 2.0 --settle 0.5 --service-attempts 3 --topic-attempts 3
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/rjscm_two_module_check.py --left /dev/ttyACM0 --right /dev/ttyACM1 --timeout 3
/tmp/rjscm-build/see_ball_responder /dev/ttyACM1 2 true
/tmp/rjscm-build/see_ball_caller /dev/ttyACM0 1 2 2000
PYTHONPATH=python/sibcp/src python3 python/sibcp/examples/all_datatypes_example.py
scripts/setup_rc_udev.sh list
scripts/setup_rc_udev.sh install --dry-run
```

Result:

- Python tests: 26 passed.
- Python editable install/import/CLI smoke: passed.
- C/C++/firmware protocol tests: 3 passed.
- C/C++ CMake install smoke: passed.
- Firmware build: passed.
- Live two-module USB-C hardware test: passed on `/dev/ttyACM0` and `/dev/ttyACM1`.
- High-level Python `rjscm` wrapper hardware test: passed on `/dev/ttyACM0` and `/dev/ttyACM1`.
- C/C++ `rjscm.h` wrapper hardware test: passed on `/dev/ttyACM0` and `/dev/ttyACM1`.
- Datatype topic/service example: passed for all 14 Python schema shapes.
- udev stable-name helper: detected both modules and generated `/dev/ttyRC0`
  and `/dev/ttyRC1` dry-run rules using USB identity plus physical port path.

Follow-up fixes completed after the initial audit:

- Python service responses now match `(transaction_id, service_id)` and
  transaction IDs are allocated while holding the pending-call lock.
- C/C++ public definitions now reject duplicate custom topic/service IDs; the
  built-in standard aliases remain supported through internal alias registration.
- Firmware now drops externally received reserved system topic/service frames in
  both host-to-radio and radio-to-host directions.
- Firmware now tracks ESP-NOW init/send/RX/drop diagnostics and emits periodic
  BLE diagnostic log summaries.
- PR CI, package install smoke tests, CMake install support, README quickstarts,
  ID allocation docs, hardware checklist, PR draft, and release-note warning are
  now integrated.
- The informal `stm_init()` timer comment was replaced with a production comment.
- The high-level Python `rjscm` wrapper was hardware-tested over two USB-C modules.
- The C/C++ `rjscm.h` wrapper examples were made buildable/configurable and
  hardware-tested over two USB-C modules.
- The datatype reference and executable datatype examples now cover every
  supported Python custom topic/service schema type.
- The Linux udev helper now documents and generates stable physical-port
  symlinks without binding to each module serial number.

## 1. Executive Summary

The repository now contains an ESP32-C5 communication-module firmware, a binary
SIBCP protocol layer, Python client libraries, a high-level `rjscm` Python API,
and a new C/C++ `rjscm.h` wrapper. The direction is good: the firmware stays a
transport bridge, while robot-side libraries own application-level topic and
service meaning. The new wrapper APIs make the feature much more accessible for
student teams.

The biggest current risks are not basic compilation issues; the code builds and
unit tests pass. The main risks are system-level: unauthenticated BLE commands,
unauthenticated broadcast ESP-NOW traffic, limited reliability around packet
loss/backpressure, and duplicated protocol logic across firmware/Python/C that
can drift.

The code is suitable for controlled experimentation. It is not yet match-ready
against adversarial wireless conditions. The USB-C two-module path and Python
wrapper have passed bench hardware checks, but UART1, BLE coexistence, and
longer radio soak testing still need validation.

Most important risks:

1. BLE referee commands are unauthenticated and can change robot state.
2. ESP-NOW forwarding is broadcast, unencrypted, and unauthenticated.
3. SIBCP protocol logic is duplicated in firmware, Python, and C/C++.
4. Reliability is best-effort: no ACKs, retries, duplicate filtering, or peer
   addressing yet. Diagnostics exist, but delivery is still not guaranteed.
5. Hardware validation still remains for UART1, BLE coexistence, longer packet
   loss testing, and two separate Raspberry Pis.

## 2. Architecture Review

### Strengths

- Clear high-level direction: firmware bridges framed SIBCP over UART/USB and
  ESP-NOW; robot libraries interpret topics/services.
- SIBCP has bounded payloads, magic bytes, CRC validation, and streaming parsers.
- Python has a low-level `SibcpNode` and high-level `rjscm.Module`, which is a
  reasonable layering.
- C/C++ wrapper is source-only and simple to embed in Raspberry Pi projects.
- The code now includes student-facing docs and tests for Python and C/C++ APIs.
- Firmware-owned local services (`/system/set_led`, `/system/play_melody`) are
  handled locally instead of forwarded as peer robot services.

### Issues

| Severity | Component | Issue | Why it matters | Suggested fix |
|---|---|---|---|---|
| High | `firmware/RCj_comm_module/interbot_comm.cpp` | ESP-NOW is configured as broadcast with `peer.encrypt = false` at lines 145-150. | Any nearby capable radio can inject or replay valid-looking SIBCP frames. CRC only protects accidental corruption. | Move to configured unicast peers, enable ESP-NOW encryption where feasible, and add app-layer authentication with sender ID and sequence number. |
| Resolved | `firmware/RCj_comm_module/interbot_comm.cpp`, `sibcp_protocol.cpp` | External reserved system topic/service frames are now filtered before forwarding or exposing to local hosts. | Prevents peer-originated `/system/game_state`, score, time, referee-event, and local system service spoofing through SIBCP. | Keep the new `firmware_sibcp_protocol` test in CI and document reserved ID ownership. |
| High | `firmware/RCj_comm_module/ble.cpp`, `ble_processing.cpp` | BLE RX characteristic is write/write-no-response without security at `ble.cpp` lines 114-118, and BLE commands directly set state at `ble_processing.cpp` lines 101-117. | A non-referee BLE central can send STOP, DAMAGE, GAME_OVER, etc. This can disable or manipulate a robot. | Add physical pairing mode, bonding, command authorization, and ideally app-layer HMAC/nonce validation. |
| Medium | Protocol architecture | SIBCP framing and CRC logic are implemented independently in firmware (`sibcp_protocol.cpp`), Python (`protocol.py`), and C (`rjscm.c`). | Behavior can drift silently; future protocol changes must be manually replicated in three places. | Add a protocol spec file and shared golden vectors. Run cross-language encode/decode tests in CI. |
| Resolved | Build/CI | Added `.github/workflows/ci.yml` for PR/push checks. | Python package install/imports, Python tests, C/C++ tests/install, and firmware build are now covered by CI scaffolding. | Watch first upstream CI run because it uses GitHub-hosted ESP-IDF action resources. |
| Resolved | Product docs | Main `README.md` now shows CLI service commands, Python `import rjscm`, C/C++ `#include "rjscm.h"`, install notes, and links to detailed guides. | Students see the high-level path first and are warned about ESP-NOW limitations. | Keep examples synchronized with wrapper APIs. |

## 3. Code Quality Review

### Strengths

- Python low-level protocol code is small, testable, and readable.
- Python `rjscm` wrapper follows the existing `sibcp` abstractions instead of
  reimplementing framing.
- C wrapper has a clear opaque-handle C ABI and C++ RAII wrapper.
- Firmware SIBCP parser is bounded and avoids dynamic allocation in hot paths.
- Tests use in-memory/socket transports, which is appropriate for protocol
  behavior.

### Issues

| Severity | Component | Issue | Why it matters | Suggested fix |
|---|---|---|---|---|
| Resolved | `python/sibcp/src/sibcp/node.py` | Service responses are now matched by both transaction ID and service ID. | A stray or malicious response with the same transaction ID but wrong service ID no longer completes the wrong call. | Keep `test_service_response_must_match_service_id` in CI. |
| Resolved | `python/sibcp/src/sibcp/node.py` | Transaction IDs for service calls are now allocated while holding the pending-call lock. | Reduces race risk for concurrent service callers. | Document thread-safety expectations if broader concurrent use is supported later. |
| Resolved | `c/rjscm/src/rjscm.c` | Public `rjscm_define_message()` now rejects duplicate custom topic IDs; standard aliases are internal-only. | Accidental duplicate custom topics no longer create ambiguous callbacks. | Keep duplicate-ID regression coverage in the C test. |
| Resolved | `c/rjscm/src/rjscm.c` | Public `rjscm_define_service()` now rejects duplicate custom service IDs; standard aliases are internal-only. | Accidental duplicate custom services no longer create ambiguous request routing. | Keep duplicate-ID and alias-call regression coverage in the C test. |
| Resolved | `c/rjscm/include/rjscm.h` | `RJSCM_PACKED` is now available for custom C payload structs and documented in the C/C++ guides. | Students have an explicit way to avoid compiler padding in fixed binary payloads. | Prefer simple fixed-width fields and keep schemas documented on both robots. |
| Medium | `python/sibcp/src/rjscm/module.py` | `PeriodicPublisher` uses a background thread per topic at lines 80-133. | Many high-rate topics can create thread overhead and timing jitter on small controllers. Provider exceptions are only logged if a logger exists. | Consider one scheduler thread per module, expose publisher health, and add rate limits/backpressure guidance. |
| Resolved | `firmware/RCj_comm_module/state_machine.cpp` | The informal `stm_init()` timer comment was replaced with a clear default-timer comment. | Removes an unprofessional code comment and documents that state-specific timers overwrite the default. | Keep timer behavior covered in state-machine docs. |
| Low | Style consistency | Firmware mixes Arduino style, ESP-IDF style, C-style globals, and new protocol code without a clear local style guide. | Future contributors will have difficulty knowing what pattern to follow. | Add a short firmware style note and keep new code consistent around naming, error handling, and comments. |

## 4. Testing and Reliability Review

### Strengths

- Python protocol/node tests cover topics, services, system topics, CLI, and
  high-level wrapper behavior.
- C/C++ wrapper tests cover custom messages, service calls, and periodic topics
  through socket transports.
- Firmware builds successfully with the current ESP-IDF setup.
- A hardware integration test script exists for two USB-C modules.

### Issues

| Severity | Component | Issue | Why it matters | Suggested fix |
|---|---|---|---|---|
| Resolved | `firmware/RCj_comm_module/interbot_comm.cpp` | ESP-NOW send return values, send callback results, RX validation, queue drops, reserved-frame drops, and init status are now counted. | Packet drops and send failures are visible through periodic BLE diagnostic logs. | Hardware-test the counters during packet-loss and BLE coexistence tests. |
| High | Inter-bot protocol | No ACK, retry, duplicate filtering, sequence number, or sender filtering exists for ESP-NOW frames. | Topic/service messages can be lost, replayed, duplicated, or delivered from unintended modules. Services can time out under normal RF loss. | Add sender ID, sequence number, optional ACK/retry for service frames, duplicate cache, and peer addressing. |
| Resolved | CI/test coverage | Added PR/push CI workflow for Python, C/C++, install smoke, and firmware build. | Regressions should be caught before release tags. | Confirm the first upstream CI run succeeds. |
| Medium | Hardware coverage | Unit tests use memory/socket transports; two-module USB-C hardware checks are manual scripts. | UART1, BLE coexistence, and timing interactions can regress without detection. | Keep the hardware scripts, but define repeatable manual test evidence for each PR. If possible, add a lab-run integration job or nightly hardware test. |
| Resolved | `python/sibcp/tests` | Added CI and local verification for editable package install/import smoke. | Packaging regressions are now easier to catch. | Keep unit tests importable both from source tree and installed package. |
| Resolved | C/C++ wrapper | Added install targets and `rjscm::rjscm` CMake package export. | Teams can consume the wrapper through normal CMake install/find_package flow. | Consider packaging/release artifacts after upstream review. |
| Low | Periodic topic publishing | C `rjscm_update()` runs periodic provider callbacks before reading serial at lines 276-294 and returns immediately on provider error at lines 1440-1448. | A slow or failing provider can delay reads and service responses. | Document provider constraints; consider isolating provider errors and continuing I/O. |

## 5. Risks and Bugs

### Confirmed bugs or high-risk behavior

1. **Fixed: Python service response matching was incomplete.**
   - Component: `python/sibcp/src/sibcp/node.py`
   - Issue: `_handle_service_response()` accepts a response based only on
     transaction ID.
   - Why it matters: wrong or spoofed response IDs can complete the wrong call.
   - Fix: pending calls are keyed by `(transaction_id, service_id)`, and a
     regression test injects a wrong service ID before the correct response.

2. **Fixed: firmware forwarded reserved system topics from external sources.**
   - Component: `firmware/RCj_comm_module/interbot_comm.cpp`
   - Issue: radio frames and host frames are forwarded after frame validation
     without filtering reserved system topic IDs.
   - Why it matters: robot code may trust spoofed referee/game-state data.
   - Fix: `sibcp_is_reserved_external_frame()` blocks external reserved system
     topics/services, with a host-side protocol regression test.

3. **Wireless transport is unauthenticated and best-effort.**
   - Component: ESP-NOW bridge and BLE command path.
   - Issue: broadcast ESP-NOW has no encryption/authentication; BLE commands are
     unauthenticated.
   - Why it matters: nearby devices can inject commands or data; RF loss is not
     visible.
   - Suggested fix: staged hardening: BLE bonding/app auth, ESP-NOW unicast,
     MAC/sequence number, diagnostics, and retry behavior.

4. **Fixed: C/C++ wrapper custom payload model lacked public packing guidance.**
   - Component: `c/rjscm/include/rjscm.h`
   - Issue: custom messages are raw structs; built-ins are packed but user
     structs are not forced to be packed.
   - Why it matters: layout differences and padding can corrupt payload meaning.
   - Fix: `RJSCM_PACKED` is now public and documented for custom C structs.

5. **Fixed: duplicate ID handling differed between Python and C.**
   - Component: Python rejects duplicate low-level IDs; C permits duplicates
     with matching payload sizes.
   - Why it matters: different languages can behave differently for the same
     mistake.
   - Fix: public C definitions now reject duplicate custom IDs while keeping
     standard aliases registered through internal helpers.

### Risky assumptions

- Only two modules are active.
- Broadcast ESP-NOW frames are acceptable.
- The local robot host connected over USB/UART is trusted.
- CRC-16 is enough for frame validity.
- Students will keep matching schemas and IDs manually.
- Python threads are acceptable for every periodic topic.
- C/C++ custom structs have identical layout on both robots.

## 6. Recommended Actions

### Top 5 priorities

1. **Create a shared protocol conformance suite.**
   - Define golden SIBCP frames and expected decoded values.
   - Use the same vectors in firmware, Python, and C/C++ tests.
   - Keep using `docs/sibcp_id_allocation.md` as the canonical ID allocation reference.

2. **Harden wireless security.**
   - Add BLE bonding/app-layer authentication.
   - Move ESP-NOW toward authenticated peer traffic.
   - Add sequence numbers and replay protection.

3. **Add real transport reliability.**
   - Add ACK/retry behavior for service frames.
   - Add sender IDs, peer addressing, duplicate filtering, and timeouts.
   - Keep topics best-effort but sequence them.

4. **Continue hardware validation.**
   - Keep Python and C/C++ wrapper checks in the manual release checklist.
   - Test UART1 IO4/IO5 with a robot controller.
   - Test two separate Raspberry Pis and BLE coexistence.

5. **Decide PR scope.**
   - Either keep the full feature branch together or split buzzer/status LED,
     system topics, wrappers, and Wi-Fi bridge into smaller PRs.

### Near-term PR cleanup

- Decide whether this PR should include firmware buzzer/status LED changes,
  SIBCP bridge changes, Python wrapper, and C/C++ wrapper together. The current
  branch is large and crosses several ownership areas.
- Stage all intended untracked files, including docs and `todo.md`, or split
  them into separate PRs.
- Use `docs/pr_wifi_module_communication.md` as the PR description starting point.
- Use `docs/two_module_hardware_checklist.md` for manual validation before opening the PR.

### Medium-term improvements

- Add peer addressing and behavior for more than two modules.
- Add ACK/retry behavior for service calls; keep topics best-effort but add
  sequence numbers.
- Add BLE pairing/bonding and app-layer authentication.
- Add key provisioning plan for teams.
- Add installable/release artifacts after upstream package expectations are known.

### Product/delivery recommendations

- Present the high-level wrappers as the default user path:
  - Python: `import rjscm`
  - C/C++: `#include "rjscm.h"`
- Keep low-level `sibcp` docs available for advanced users.
- Provide one "two robots over USB-C" quickstart with commands and expected
  serial/device names.
- Clearly label ESP-NOW inter-bot communication as experimental until security
  and reliability work is complete.
