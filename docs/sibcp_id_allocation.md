# SIBCP ID Allocation

SIBCP frames use one byte called `identifier_id`.

- For topic frames, it is the topic ID.
- For service request and response frames, it is the service ID.

Both robots must use the same IDs and payload schemas.

## Reserved Ranges

| Range | Owner | Use |
|---|---|---|
| `0x00` | Protocol | Reserved. Do not use for team data. |
| `0x01` - `0x1F` | Standard robot APIs | Built-in team-facing topics/services such as `see_ball` and `request_role`. |
| `0x20` - `0xEF` | Teams | Custom topics and services. |
| `0xF0` - `0xFF` | Firmware/system | Local module system topics and services. |

## Standard IDs

Topics:

| ID | Path | Payload |
|---|---|---|
| `0x10` | `/robot/pose` | Robot pose |
| `0x11` | `/world/ball` | Ball estimate |
| `0x12` | `/world/opponent` | Opponent estimate |
| `0xF0` | `/system/game_state` | Referee game state |
| `0xF1` | `/system/score` | Score |
| `0xF2` | `/system/match_time` | Half and remaining time |
| `0xF3` | `/system/referee_event` | Goal, penalty, half, or match event |

Services:

| ID | Path | Payload |
|---|---|---|
| `0x01` | `/ball_in_your_vision` / `see_ball` | Empty request, boolean response |
| `0x10` | `/request_role` | Empty request, tactical-role response |
| `0xF0` | `/system/set_led` | Local-only LED command |
| `0xF1` | `/system/play_melody` | Local-only melody command |

## Rules For Team IDs

- Use `0x20` through `0xEF` for custom topics and services.
- Do not reuse one ID for two different custom names.
- Keep topic IDs and service IDs documented in your team code.
- Keep payload field sizes fixed, for example `int32_t`, `uint8_t`, or Python
  schema fields.
- For C payload structs, use `RJSCM_PACKED`.
- Prefer the built-in helper paths where they match your use case.

## System IDs Are Local Authority

The firmware owns `0xF0` through `0xFF`. The module sends local system topics to
the robot host, and consumes local system services such as LED and melody
commands. Externally received system IDs are dropped by the firmware bridge so a
peer module cannot spoof referee/game-state data through SIBCP.
