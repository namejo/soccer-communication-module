# 13 — SIBCP Future Ideas

This document captures possible next features for the SIBCP robot-to-robot and
module-to-robot layer. These are design notes, not implemented behavior.

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

Suggested payload:

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

Suggested payload:

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

### Existing: `/system/game_state`

Already implemented as reserved topic ID `0xF0`.

Payload:

| Field | Type | Meaning |
|-------|------|---------|
| `state` | `uint8` | init, disconnected, play, stop, damage, half-time, game-over |
| `robot_play` | `bool` | direct GO/STOP output state |

### Proposed: `/system/match_time`

Expose remaining time so robots can adjust behavior near the end.

Suggested payload:

| Field | Type | Meaning |
|-------|------|---------|
| `half` | `uint8` | `1` first half, `2` second half, `0` unknown |
| `remaining_ms` | `uint32` | Remaining time in current phase |
| `phase_total_ms` | `uint32` | Full duration of the current phase if known |

Use cases:

- More aggressive attack when behind late in the match.
- Safer defensive behavior when ahead near the end.

### Proposed: `/system/score`

Expose current score.

Suggested payload:

| Field | Type | Meaning |
|-------|------|---------|
| `own_score` | `uint8` | Current team's score |
| `opponent_score` | `uint8` | Opponent score |

Use cases:

- If losing, choose higher-risk tactics.
- If winning, prefer ball control and defense.

### Proposed: `/system/referee_event`

Short event topic for important referee-side changes.

Suggested event values:

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

Suggested request:

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

Suggested request:

| Field | Type | Meaning |
|-------|------|---------|
| `melody_id` | `uint8` | Predefined melody stored in firmware |
| `repeat` | `uint8` | Repeat count, capped |
| `volume` | `uint8` | Optional future field, 0 to 100 |

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

## Open Design Questions

1. Should system topic IDs live in firmware headers, Python constants, or a generated shared
   schema file?
2. Should robot topics use broadcast only, or should the protocol grow addressing before more
   world-model topics are added?
3. How should stale data be handled: timestamps only, TTL in the library, or module-level
   filtering?
4. Should `/system/set_led` be disabled during STOP or only allowed to use dim status-safe
   colors?
5. Should melodies be predefined by ID for safety and simplicity, or should teams be allowed
   to upload arbitrary tone sequences?
6. Should score and time come directly from the referee app over BLE, or should the module
   derive them from existing BLE messages?

## Suggested Next Implementation Order

1. Add schema constants to the Python library for `/system/game_state`, `/system/score`, and
   `/system/match_time`.
2. Extend firmware to emit `/system/score` whenever BLE score messages change.
3. Extend firmware to emit `/system/match_time` during penalty, half-time, and match phases
   once the app provides enough timing information.
4. Add robot-owned example topics: `/robot/pose` and `/world/ball`.
5. Add `/system/set_led` as a guarded service.
6. Add `/system/play_melody` with predefined melody IDs first; consider programmable tones
   only after hardware testing.
