# 13 — SIBCP Future Ideas

This document captures SIBCP robot-to-robot and module-to-robot feature ideas.
Several items are now implemented on `feature_wifi_module_communication`; remaining
notes are called out as future work.

## Implementation Status

Implemented:

- Python schema helpers for `/robot/pose`, `/world/ball`, `/world/opponent`,
  `/ball_in_your_vision`, and `/request_role`.
- Firmware system topics for `/system/game_state`, `/system/score`,
  `/system/match_time`, and `/system/referee_event`.
- Firmware-owned local services `/system/set_led` and `/system/play_melody`.
  `/system/set_led` is rejected outside PLAY so referee STOP feedback wins.

Still future work:

- Addressing, peer identity, duplicate filtering, ACK/retry, and Wi-Fi authentication.
- Programmable melody upload; firmware currently supports predefined melody IDs only.
- Full match-duration tracking beyond timer values already supplied by BLE penalty and
  half-time commands.
- Optional non-robot "base station" listeners for commentary, logging, and audio effects.

## Goals

- Let two team robots share useful world-model data with low latency.
- Expose referee/system state as structured SIBCP topics instead of ad-hoc serial text.
- Keep robot strategy optional: teams can ignore this data or use it to adapt tactics.
- Keep firmware-owned features clearly separate from robot-owned application topics.

## Suggested Namespace Split

| Namespace | Owner | Purpose |
|-----------|-------|---------|
| `/robot/...` | robot code | Team-specific robot perception and strategy data |
| `/world/...` | robot code | Shared field model observations |
| `/system/...` | communication module firmware | Referee state, score, time, module feedback controls |
| `/debug/...` | robot code or firmware | Non-match debug telemetry |

The binary SIBCP frame still carries only numeric IDs. These paths are Python/client-library
names that must map to agreed IDs on both robots.

## Robot-to-Robot World Model Topics

### `/robot/pose`

Robot publishes its own estimated field position.

Payload:

| Field | Type | Meaning |
|-------|------|---------|
| `x_mm` | `int16` or `int32` | Field X position in millimeters |
| `y_mm` | `int16` or `int32` | Field Y position in millimeters |
| `heading_mrad` | `int16` | Heading in milliradians |
| `confidence` | `uint8` | 0 to 100 |
| `timestamp_ms` | `uint32` | Robot-local timestamp |

Use cases:

- Avoid both robots driving to the same place.
- Let one robot defend while the other attacks.
- Detect stale teammate data by timestamp.

### `/world/ball`

Robot publishes its current ball estimate.

Payload:

| Field | Type | Meaning |
|-------|------|---------|
| `visible` | `bool` | Whether this robot currently sees the ball |
| `x_mm` | `int16` or `int32` | Ball X position estimate |
| `y_mm` | `int16` or `int32` | Ball Y position estimate |
| `confidence` | `uint8` | 0 to 100 |
| `timestamp_ms` | `uint32` | Robot-local timestamp |

This can replace or extend the simple `/ball_in_your_vision` boolean service.

### `/world/opponent`

Robot publishes observed opponent position or threat estimate.

Suggested payload:

| Field | Type | Meaning |
|-------|------|---------|
| `visible` | `bool` | Whether an opponent was detected |
| `x_mm` | `int16` or `int32` | Estimated opponent X position |
| `y_mm` | `int16` or `int32` | Estimated opponent Y position |
| `threat` | `uint8` | 0 to 100, robot-defined |
| `timestamp_ms` | `uint32` | Robot-local timestamp |

Use cases:

- Defending robot can block likely opponent path.
- Attacking robot can choose a safer lane.

## Robot-to-Robot Services

### `/ball_in_your_vision`

Simple boolean service for teams that do not want a full ball topic.

Request: empty.

Response:

| Field | Type |
|-------|------|
| `visible` | `bool` |

### `/request_role`

One robot asks the peer which tactical role it is currently taking.

Suggested response:

| Value | Role |
|-------|------|
| `0` | unknown |
| `1` | attacker |
| `2` | defender |
| `3` | goalie/support |
| `4` | searching |

This avoids both robots independently choosing the same role.

## System Topics From The Module

### `/system/game_state`

Implemented as reserved topic ID `0xF0`.

Payload:

| Field | Type | Meaning |
|-------|------|---------|
| `state` | `uint8` | init, disconnected, play, stop, damage, half-time, game-over |
| `robot_play` | `bool` | direct GO/STOP output state |

### `/system/match_time`

Implemented as reserved topic ID `0xF2`. It exposes countdown information for phases where
the firmware receives a timer, currently penalty/damage and half-time.

Suggested payload:

| Field | Type | Meaning |
|-------|------|---------|
| `half` | `uint8` | `1` first half, `2` second half, `0` unknown |
| `remaining_ms` | `uint32` | Remaining time in current phase |
| `phase_total_ms` | `uint32` | Full duration of the current phase if known |

Use cases:

- More aggressive attack when behind late in the match.
- Safer defensive behavior when ahead near the end.

### `/system/score`

Implemented as reserved topic ID `0xF1`. The firmware emits it when BLE score messages or
game-over messages update the score.

Suggested payload:

| Field | Type | Meaning |
|-------|------|---------|
| `own_score` | `uint8` | Current team's score |
| `opponent_score` | `uint8` | Opponent score |

Use cases:

- If losing, choose higher-risk tactics.
- If winning, prefer ball control and defense.

### `/system/referee_event`

Implemented as reserved topic ID `0xF3`. It is a short event topic for important
referee-side changes.

Event values:

| Value | Event |
|-------|-------|
| `1` | goal scored by own team |
| `2` | goal scored by opponent |
| `3` | penalty started |
| `4` | penalty ended |
| `5` | half started |
| `6` | match ended |

This is useful for one-shot actions like sounds, logs, or strategy resets.

## System Services On The Module

These should be firmware-owned because they control module hardware.

### `/system/set_led`

Let robot code request LED feedback.

Implemented request:

| Field | Type | Meaning |
|-------|------|---------|
| `mode` | `uint8` | off, solid, blink, pulse |
| `red` | `uint8` | 0 to 255 |
| `green` | `uint8` | 0 to 255 |
| `blue` | `uint8` | 0 to 255 |
| `duration_ms` | `uint16` | 0 means keep until next state-owned update |

Important rule: referee safety feedback should have priority. For example, STOP red should
override custom robot LED effects unless explicitly allowed.

### `/system/play_melody`

Let teams play a short custom jingle, for example after a goal.

Implemented request:

| Field | Type | Meaning |
|-------|------|---------|
| `melody_id` | `uint8` | Predefined melody stored in firmware |
| `repeat` | `uint8` | Repeat count, capped |

Volume is not implemented yet.

Alternative request for programmable tones:

| Field | Type | Meaning |
|-------|------|---------|
| `tone_count` | `uint8` | Number of tone entries |
| `tones` | bytes | Repeated `{freq_hz:uint16, duration_ms:uint16}` |

Keep this bounded. The buzzer is passive and firmware must stay non-blocking.

## Tactical Examples

- If `/system/score` says the robot is behind and `/system/match_time.remaining_ms` is low,
  switch to aggressive ball chasing.
- If the robot is ahead late in the match, prefer defensive positioning near own goal.
- If peer `/world/ball.visible` is true and local ball is not visible, move into support
  position instead of blind searching.
- If peer `/robot/pose` is close to local target, choose a different route or role.
- If `/system/referee_event` reports goal scored, play `/system/play_melody` with a team
  jingle and reset local strategy state.

## Base Station And Commentary Ideas

If future firmware and the referee app can decode and display custom SIBCP messages, the same
radio traffic could also support a non-robot listener. This could be called a base station,
commentary station, or event station.

Possible setup:

```text
robots/modules -> ESP-NOW SIBCP traffic -> base station module -> USB/BLE/app/audio device
```

Use cases:

- Live match commentary based on decoded robot topics and services.
- Referee-app logs that show team-defined events such as "robot 1 sees ball" or
  "defender switched role".
- A separate ESP32 audio module with an audio jack or DAC that listens for system events and
  plays prerecorded sound files.
- Goal, half-time, match-start, and match-end jingles without putting extra audio hardware on
  every robot.
- Debug replay after a match by recording decoded SIBCP events with timestamps.

### AI Live Commentary

The base station could also transform decoded event streams into live commentary. One possible
architecture is to collect structured SIBCP events from both teams, filter them into a compact
match timeline, and stream that timeline to a low-latency speech system such as the OpenAI
Realtime API:

https://openai.com/index/introducing-the-realtime-api/

Example event flow:

```text
team A/B custom topics + system referee events
  -> base station decoder
  -> rate-limited event timeline
  -> live commentary prompt/context
  -> generated speech for spectators or a stream overlay
```

Useful event inputs:

- `/system/referee_event` for goals, half-time, penalties, and match end.
- `/system/score` and `/system/match_time` for context-aware commentary.
- Robot-owned topics such as `/world/ball`, `/robot/pose`, role changes, or custom
  "shot attempt" and "defensive block" events.
- Service-call summaries, for example "robot 1 asked robot 2 whether it sees the ball".

Constraints for AI commentary:

- Do not stream raw high-rate telemetry directly. Aggregate and rate-limit events first.
- Avoid private team debugging data unless teams explicitly opt in.
- Make clear to spectators that commentary is AI-generated if it is not obvious from context.
- Keep the AI/base-station path read-only; it must not send commands back to robots or modules.
- Store only the minimum event log needed for replay/debugging.

Important design constraints:

- A base station should be receive-only during matches unless there is a clear authorization
  model. It must not be able to spoof robot commands or referee/system topics.
- It should use the same schema/ID registry as the teams so custom payloads can be decoded
  without guessing.
- More than two active modules requires peer identity and addressing, otherwise a base station
  cannot reliably tell which robot produced each event.
- Audio playback should be driven by high-level events such as `/system/referee_event` or
  team-defined event topics, not by every raw packet.
- For public commentary, rate limits and filtering are needed so telemetry topics do not flood
  the app or audio device.

## Open Design Questions

1. Should system topic IDs continue to be duplicated in firmware headers and Python constants,
   or should they move to a generated shared schema file?
2. Should robot topics use broadcast only, or should the protocol grow addressing before more
   world-model topics are added?
3. How should stale data be handled: timestamps only, TTL in the library, or module-level
   filtering?
4. Should `/system/set_led` be disabled during STOP or only allowed to use dim status-safe
   colors?
5. Should teams be allowed to upload arbitrary tone sequences, or are predefined melody IDs
   enough for match use?
6. Should match time include full running match time from the app, or only the countdown phases
   the firmware already receives?
7. Should a base station be supported as a first-class read-only peer, and how should it obtain
   the shared schema/ID registry for custom team messages?
8. Should the referee app show custom team messages directly, or should it forward decoded
   events to an external commentary/audio station?

## Suggested Next Implementation Order

1. Add per-peer addressing and a module identity field so more than two modules can share one
   field without processing their own broadcasts.
2. Add ESP-NOW authentication/encryption or an application-level message authentication code
   before considering match use.
3. Add duplicate filtering and service-call timeouts/retries for crowded fields.
4. Add richer match time once the referee app exposes enough timing information.
5. Consider programmable melodies only after predefined melodies are proven safe and useful.
6. Consider read-only base-station/commentary support after addressing, authentication, and
   schema registry work exists.
