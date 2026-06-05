# Security Audit

This document describes the current security posture of the ESP32-C5 soccer
communication module firmware and the experimental SIBCP inter-bot transport.

Status: **BLE command writes are hardened with authenticated pairing on the
`BLE_secure` branch; the experimental ESP-NOW inter-bot transport is still not
hardened against adversarial wireless attacks**.

## Direct Answers

### Can someone interfere with Bluetooth and disable a bot?

**Unauthenticated BLE command injection is mitigated on the `BLE_secure`
branch. RF denial of service is still possible.**

The BLE command service now requires BLE Secure Connections pairing with MITM
protection and bonding. The module generates a six-digit passkey at boot, shows
it on the OLED, includes it in the connection QR payload, and requires
authenticated encrypted writes for the RX characteristic. A BLE central that has
not paired with the displayed passkey should not be able to write referee
commands such as `PLAY`, `STOP`, `DAMAGE`, `HALF_BREAK`, or `GAME_OVER`.

Evidence:

- `ble.cpp` configures `ESP_LE_AUTH_REQ_SC_MITM_BOND`, display-only IO
  capability, a per-boot random passkey, and `ESP_GATT_PERM_WRITE_ENC_MITM` on
  the RX characteristic.
- `ble.cpp` rejects RX writes unless the NimBLE connection reports encrypted,
  authenticated security with a 16-byte key.
- `ble_processing.cpp` validates per-command payload lengths before state
  changes are dispatched.

There is also an RF denial-of-service angle: sustained BLE interference or link
loss can eventually trigger disconnect handling. The firmware intentionally
fails safe by moving to `STM_DISCONNECTED`, which stops the robot output. That is
the correct safety behavior, but it means RF disruption can still disable play.

### Is the Wi-Fi communication between two modules safe from MITM/injection?

**No, not currently.**

The current ESP-NOW bridge uses broadcast frames and disables ESP-NOW peer
encryption:

```cpp
peer.peer_addr = ff:ff:ff:ff:ff:ff
peer.encrypt = false
```

SIBCP validates frame shape and CRC-16, but CRC is not authentication. Anyone who
knows the public frame format can compute a valid CRC.

A normal Wi-Fi access point "evil twin" is not the exact threat model here,
because the modules do not associate to an AP for ESP-NOW. However, a capable
nearby radio can still sniff, replay, relay, or inject ESP-NOW-style frames on
the configured channel. The current firmware has no cryptographic message
authentication, no replay protection, no source MAC enforcement, and no duplicate
filtering.

## Threat Model

| Threat | Current status |
|--------|----------------|
| Nearby BLE central sends referee commands | **Protected by authenticated pairing on `BLE_secure`** |
| Nearby BLE jammer causes disconnect/fail-safe stop | **Not preventable in firmware** |
| Nearby Wi-Fi/ESP-NOW injector sends SIBCP frames | **Not protected** |
| Passive observer reads ESP-NOW SIBCP payloads | **Not protected** |
| Replay of old SIBCP service/topic frames | **Not protected** |
| Physical host on USB-C or UART sends frames | **Trusted physical access** |
| Accidental radio corruption | **CRC and parser protect against this** |

## Current Controls

The current code does include basic reliability protections:

- BLE input length is capped by `BLE_DATA_MAX_LENGTH`.
- BLE command writes require Secure Connections pairing with MITM protection.
- BLE command payloads are validated per message type before use.
- BLE command queue is bounded and drops old commands if full.
- SIBCP frames require magic bytes, bounded payload length, and CRC-16/CCITT.
- ESP-NOW receive revalidates SIBCP frames before forwarding them to the robot.
- UART/USB streaming parsers resynchronize on valid SIBCP frame boundaries.
- BLE disconnect fails safe to a stopped robot output.

These are useful reliability controls, but they are **not security controls**
against an active attacker.

## Findings

### Mitigated on `BLE_secure`: BLE referee commands were unauthenticated

Original impact:

- A nearby device that connects as the BLE central can send `STOP`, `DAMAGE`,
  `HALF_BREAK`, or `GAME_OVER`.
- A malicious or buggy client can control the match state without proving it is
  the referee app.

Implemented mitigation:

- BLE Secure Connections with MITM protection and bonding.
- Display-only passkey flow: the app/phone must enter the six-digit PIN shown
  by the module.
- Authenticated encrypted write permission on the RX characteristic.
- Software-side authenticated-connection check before queueing commands.

Remaining hardening options:

1. Require a physical pairing window, for example hold a module button to allow
   new referee-app pairing.
2. Store and prefer a bonded referee device identity.
3. Consider app-layer authentication as well: command nonce + truncated HMAC
   over `msg_id || payload`.

### Mitigated on `BLE_secure`: BLE malformed payloads were not validated per command

Original impact:

- `BLE_MSG_SET_NAME`, `BLE_MSG_SET_SCORE`, `BLE_MSG_DAMAGE`,
  `BLE_MSG_HALF_BREAK`, and `BLE_MSG_GAME_OVER` read fixed payload indexes but
  do not first verify `data_length`.
- Short malformed commands can use uninitialized payload bytes and produce
  unpredictable timers or scores.

Implemented mitigation:

- A per-message expected length table rejects commands whose payload length does
  not match before the state machine is touched.

### High: ESP-NOW inter-bot transport has no authentication or encryption

Current impact:

- An attacker can inject a valid-looking SIBCP frame if they can transmit on the
  same channel and produce the CRC.
- An attacker can replay previously observed frames.
- An attacker can read robot-to-robot data such as ball/pose messages if those
  topics are added.

Recommended mitigations:

1. Move from broadcast to configured unicast peers.
2. Enable ESP-NOW peer encryption with configured PMK/LMK material where
   feasible.
3. Add app-layer authentication anyway: truncated HMAC-SHA256 or AES-CMAC over
   SIBCP header, payload, sender ID, and sequence number.
4. Add monotonically increasing sequence numbers per sender to reject replay.
5. Add sender IDs and reject frames from unknown peers.

### High: CRC-16 is not a security mechanism

Current impact:

- CRC catches accidental corruption only.
- It does not prove sender identity.
- It does not prevent malicious modification.

Recommended mitigation:

- Keep CRC for accidental corruption/resync, but add a cryptographic MAC for
  security-sensitive frames.

### Medium: Reserved `/system/...` topics can be spoofed by external frames

Current impact:

- The firmware emits `/system/game_state` locally, but the bridge forwards any
  valid SIBCP frame received from radio or host serial.
- A peer or attacker could send a frame using the reserved system topic ID
  `0xF0`, and robot-side code may treat it as firmware truth unless the client
  library or firmware filters it.

Recommended mitigations:

1. Reserve a firmware-owned ID range, for example `0xF0` to `0xFF`.
2. Reject external frames using firmware-owned system topic IDs.
3. If future system services are needed, handle them locally in firmware instead
   of blindly forwarding them over ESP-NOW.
4. Include source information in the Python library API so robot code can tell
   local module system messages from peer robot messages.

### Medium: No replay or duplicate handling

Current impact:

- A stale service request/response or topic can be replayed.
- More than two modules can create duplicate or looping behavior.

Recommended mitigations:

- Add sender ID + sequence number.
- Maintain a small recent-frame cache per sender.
- Drop repeated sequence numbers and old timestamps.

### Medium: USB-C and UART are trusted local transports

Current impact:

- A host connected over USB-C or UART1 can inject SIBCP frames.
- This is acceptable if the robot controller is trusted, but it is not safe
  against public physical access.

Recommended mitigations:

- Treat USB-C/UART access as trusted physical access.
- Do not expose USB-C during a match unless it is connected to the team's own
  controller.
- If needed, add the same app-layer authentication to SIBCP frames regardless of
  whether they arrive over radio, USB-C, or UART.

### Low: BLE debug logs leak communication metadata

Current impact:

- The BLE log characteristic can reveal SIBCP packet type, ID, transaction ID,
  and payload length to the connected BLE client.

Recommended mitigation:

- Keep debug logs disabled or restricted in release builds if this metadata is
  considered sensitive.

## Recommended Security Roadmap

### Stage 1: Input hardening

- BLE per-command payload length validation is implemented on `BLE_secure`.
- Reject external frames in the firmware-owned system ID range.
- Add rate limiting for repeated BLE commands and SIBCP frames if needed.

### Stage 2: BLE access control

- BLE pairing/bonding with passkey is implemented on `BLE_secure`.
- Add a physical pairing mode.
- Optionally add app-layer command authentication so the GATT service is not the
  only security boundary.

### Stage 3: SIBCP authentication

- Add sender ID and sequence number to SIBCP or to an authenticated envelope.
- Add a cryptographic MAC, for example truncated HMAC-SHA256 or AES-CMAC.
- Reject replayed or out-of-window sequence numbers.

### Stage 4: ESP-NOW peer hardening

- Replace broadcast with explicit configured peer MAC addresses.
- Enable ESP-NOW encryption if it works with the intended peer model.
- Keep app-layer authentication even when ESP-NOW encryption is enabled.

### Stage 5: Match-ready policy

- Decide how keys are provisioned for a team.
- Decide whether keys reset per tournament, per match, or per team.
- Document what "secure enough for competition" means for RoboCupJunior use.

## Practical Current Guidance

- Do **not** claim the current robot-to-robot Wi-Fi link is secure.
- On `BLE_secure`, BLE prevents unauthenticated command writes, but still does
  not prevent RF jamming or all denial-of-service conditions.
- Treat the current implementation as suitable for experimentation and controlled
  testing, not adversarial match environments.
- If an attacker model includes nearby radios, implement authentication before
  using SIBCP data for safety-critical or match-critical decisions.
