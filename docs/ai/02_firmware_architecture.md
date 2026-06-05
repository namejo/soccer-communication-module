# 02 — Firmware Architecture

## Module breakdown

| Module | Files | Responsibility |
|--------|-------|----------------|
| Entry point (Arduino) | `RCj_comm_module.ino` | `setup()` / `loop()` for Arduino-IDE builds |
| Entry point (ESP-IDF) | `main/app_main.cpp` | `app_main()` → `initArduino()` + setup/loop, for CI/IDF builds |
| Definitions | `definitions.h` | Versions, BLE/UART constants, GPIO pin map |
| BLE transport | `ble.cpp` / `ble.h` | NimBLE server, NUS UUIDs, RX/TX characteristics, callbacks, send/disconnect |
| BLE processing | `ble_processing.cpp` / `ble_processing.h` | `ble_msg_t`, `ble_msg_id` enum, RTOS queue, command dispatch |
| Inter-bot bridge | `interbot_comm.cpp` / `interbot_comm.h` | UART1 + USB-C SIBCP parsers, ESP-NOW setup, host <-> ESP-NOW forwarding, BLE debug logs |
| SIBCP protocol | `sibcp_protocol.cpp` / `sibcp_protocol.h` | Frame constants, CRC-16/CCITT, streaming parser, frame builder, packet metadata |
| State machine | `state_machine.cpp` / `state_machine.h` | `stm_states`, output-pin control, timer |
| Display | `display.cpp` / `display.h` | SSD1306 rendering per state |
| Status LED | `status_led.cpp` / `status_led.h` | 50% PWM RGB LED output: green for PLAY, red for stopped output |
| Buzzer | `buzzer.cpp` / `buzzer.h` | Non-blocking 2.7 kHz beep on match-state changes |
| Functions/util | `functions.cpp` / `functions.h` | MAC string, score/indicator globals, GPIO init, button handling |
| Assets | `fonts.h`, `images.h` | OLED fonts and boot logo (`RC_logo`) |

## Two parallel entry points (IMPORTANT)

There are **two** entry points that do the same four things in the same order:

`RCj_comm_module.ino` (Arduino IDE):
```c
void setup() { Serial.begin(UART_SPEED); serial_status_init(); display_init(); module_init_gpios(); stm_init(); ble_start_server(); interbot_comm_init(); }
void loop()  { ble_msg_processing(); interbot_comm_update(); stm_update(); check_disconnect_button(); check_penalty_button(); }
```

`main/app_main.cpp` (ESP-IDF — what CI actually builds):
```c
static void module_setup() { Serial.begin(UART_SPEED); serial_status_init(); display_init(); module_init_gpios(); stm_init(); ble_start_server(); interbot_comm_init(); }
static void module_loop()  { ble_msg_processing(); interbot_comm_update(); stm_update(); check_disconnect_button(); check_penalty_button(); }
extern "C" void app_main(void) { initArduino(); module_setup(); while (true) { module_loop(); delay(1); } }
```

> **Maintenance hazard:** any change to setup/loop logic must be mirrored in **both** files,
> or the Arduino-IDE and ESP-IDF builds will diverge. The release pipeline builds the
> **ESP-IDF** path (`main/app_main.cpp`); the `.ino` is for local Arduino-IDE use.

## Initialization sequence

1. `Serial.begin(UART_SPEED)` — `UART_SPEED = 115200`; UART0 is a transmit-only SIBCP
   output mirror in release builds.
2. `serial_status_init()` — installs the USB Serial/JTAG driver used by the USB-C SIBCP
   transport.
3. `ble_start_server()` — creates the message queue, BLE device, server, service, RX/TX/log
   characteristics, and starts advertising.
4. `stm_init()` — current state stays `STM_INIT`; sets timer to `660000 ms` (see below /
   [04](04_state_machine.md)).
5. `display_init()` — `SSD1306 display(0x3c, I2C_SDA_GPIO, I2C_SCL_GPIO)`, `display.init()`.
6. `module_init_gpios()` — `OUTPUT1_GPIO`/`OUTPUT2_GPIO` as `OUTPUT`; `BUTTON_GPIO`/
   `BUTTON2_GPIO` as `INPUT` (no explicit pull configured — see hardware doc).
7. `buzzer_init()` and `status_led_init()` — initialize IO26 buzzer and RGB LED PWM.
8. `interbot_comm_init()` — starts UART1 on IO4/IO5 at 460800 baud, configures 5 GHz
   ESP-NOW on channel 36, and creates bounded radio/log queues.

## Main loop behavior

`loop()` runs continuously (in IDF: `while(true) { ...; delay(1); }`). Each iteration:

1. **`ble_msg_processing()`** — pops at most **one** message from the queue (`xQueueReceive`
   with timeout `0`) and dispatches it. Non-blocking; returns immediately if empty.
2. **`interbot_comm_update()`** — drains valid SIBCP frames from UART1 and USB-C to ESP-NOW,
   drains received ESP-NOW frames to UART1/USB-C/UART0 TX, and emits queued BLE debug log
   notifications.
3. **`stm_update()`** — runs the current state's handler and, on a state change, updates
   the output pins, emits the `/system/game_state` SIBCP topic, mirrors the status to the
   RGB LED, and starts a short buzzer tone for match states. It also services the
   non-blocking buzzer timeout.
4. **`check_disconnect_button()`** — long-press (`DISCONNECT_HOLD_TIME = 5000 ms`) on
   `BUTTON_GPIO` triggers `ble_disconnect()`.
5. **`check_penalty_button()`** — double-press on `BUTTON_GPIO` **or** `BUTTON2_GPIO`
   triggers `ble_msg_procesing_ask_for_penalty()`.

There is **no explicit task split**: everything runs in the single `app_main`/loop thread.
BLE callbacks run in the NimBLE host context and only enqueue work, keeping the ISR/callback
path short.

## Concurrency / FreeRTOS

The cross-context shared structures are the BLE message queue and the inter-bot radio/log
queues.

- Created in `ble_msg_processing_init()`:
  `xQueueCreate(BLE_QUEUE_MAX_SIZE /*16*/, sizeof(ble_msg_t))`.
- **Producer:** `ble.cpp` `MyCallbacks::onWrite()` → `queue_ble_msg()` (BLE host context).
- **Consumer:** `ble_processing.cpp` `ble_msg_processing()` (main loop).

`queue_ble_msg()` (added in `ca23261`) implements an **overwrite-on-full** policy: if the
queue is full it drops the **oldest** message (`xQueueReceive`) and retries the send, so the
newest referee command is never lost to a backlog. Both `ble.cpp` and `ble_processing.cpp`
hold their own `static QueueHandle_t ble_msg_queue`; `ble.cpp` fetches the handle via
`ble_msg_proccesing_get_queue()` during `ble_start_server()`.

Other shared globals (`current_state`, `robot_play`, `timer_stop`, `module_indicator`,
`my_score`, `opponent_score`, `device_connected`) are accessed without locks. In practice
state mutations happen either in the loop or in BLE callbacks; `stm_set_state()` is called
from both `ble_processing` (loop) and `MyServerCallbacks::onDisconnect` (BLE context) —
a benign data race on `current_state`/`state_changed` that has not caused observed issues
but is worth noting for any future hardening.

Inter-bot queues:

- `radio_rx_queue`: ESP-NOW receive callback -> main loop. It holds validated raw SIBCP
  frames received over radio before they are written to UART1, USB-C, and UART0 TX.
- `log_queue`: UART/radio forwarding paths -> main loop BLE log sender. It is bounded and
  drops the oldest log entry when full, so debug logging cannot grow without limit.

## SIBCP inter-bot bridge

- UART1 uses `HardwareSerial(1)` on `INTERBOT_UART_RX_GPIO=4` and
  `INTERBOT_UART_TX_GPIO=5`, at `INTERBOT_UART_SPEED=460800`.
- USB-C uses the ESP32-C5 USB Serial/JTAG driver installed by `serial_status_init()`.
  The host commonly sees this as `/dev/ttyACM0`; the baud setting is ignored by USB.
- UART0 TX (`Serial.write`) mirrors outbound SIBCP frames at `UART_SPEED=115200` but is not
  parsed as an input transport.
- ESP-NOW runs in Wi-Fi station mode, 5 GHz only, channel `INTERBOT_WIFI_CHANNEL=36`,
  with the broadcast peer `ff:ff:ff:ff:ff:ff`.
- `sibcp_parser_push_byte()` accepts only complete frames with magic `0xAA 0x55`, payload
  length <= 240 bytes, and a valid CRC-16/CCITT.
- UART1 -> ESP-NOW, USB-C -> ESP-NOW, and ESP-NOW -> host outputs all revalidate frames.
  Invalid frames are dropped.
- Local referee state changes are emitted as `SIBCP_TOPIC_SYSTEM_GAME_STATE` (`0xF0`) with
  payload `{state:uint8, robot_play:uint8_bool}`.
- The module is a transport bridge only. Robot MCU firmware owns topic/service semantics,
  transaction matching, service discovery responses, retries, and duplicate handling.

See [12_sibcp_interbot_comm.md](12_sibcp_interbot_comm.md) for the frame format and
limitations.

## Important global state

| Variable | File | Meaning |
|----------|------|---------|
| `current_state` | `state_machine.cpp` | Active `stm_states` value |
| `state_changed` | `state_machine.cpp` | One-shot flag: forces re-render + output update next `stm_update()` |
| `robot_play` | `state_machine.cpp` | Drives `OUTPUT1/2` HIGH/LOW |
| `timer_stop` | `state_machine.cpp` | `millis()` deadline for penalty/halftime countdown |
| `buzzer_active`, `buzzer_stop_time` | `buzzer.cpp` | Non-blocking buzzer timeout state |
| `device_connected` | `ble.cpp` | BLE connection state |
| `interbot_ready` | `interbot_comm.cpp` | ESP-NOW setup status; UART parsing still runs if false |
| `module_indicator` | `functions.cpp` | 2-char team/robot label (default `"--"`) |
| `my_score`, `opponent_score` | `functions.cpp` | Scoreboard values |

## UART / Serial behavior

- `Serial.begin(115200)` is called at startup. `Serial.write()` is used only as the UART0 TX
  output mirror for binary SIBCP frames.
- `serial_status_init()` installs the USB Serial/JTAG driver. `interbot_comm_update()` reads
  valid SIBCP frames from USB-C and forwards them to ESP-NOW.
- `update_output_satet()` in `state_machine.cpp` now emits the reserved system SIBCP topic
  `/system/game_state` via `interbot_comm_send_system_game_state()`. The payload carries
  `state` (`INIT`, `DISCONNECTED`, `PLAY`, `STOP`, `DAMAGE`, `HALF_TIME`, `GAME_OVER`) and
  `robot_play` (the direct replacement for the old text `PLAY`/`STOP` output).
- All other debug prints are **already commented out**: `ble.cpp:45,64,147,149`,
  `ble_processing.cpp:71`, `functions.cpp:107,121`. (Verified 2026-05-31.) The vendored
  `libraries/**/examples/*.ino` prints are not compiled; the OLED lib's `"[deprecated]"`
  prints live in `drawLogBuffer()`/`setLogBuffer()`, which the firmware never calls.
- UART1 and USB-C are full SIBCP input/output transports. UART0 TX is output-only. The old
  2024 LOGV/A0/A1 channel-select scheme is not present on V7 hardware or in firmware.

### UART output cleanliness (for robots that read serial, not pins)

Verified against the generated build config (`build/config/sdkconfig.h`, 2026-05-31):

1. **✅ `Serial` routes to UART0 (the U3 `TX_OUT` the robot reads).** `ARDUINO_USB_CDC_ON_BOOT`
   is **not** defined, so `Serial` = `HardwareSerial(0)` = UART0; and
   `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` with `CONFIG_ESP_CONSOLE_UART_NUM=0`. SIBCP output
   frames reach UART0 TX as a binary mirror. (`ARDUHAL` log level is off in release.)
2. **✅ Boot log silenced for release (fixed 2026-05-31).** `sdkconfig.defaults` now sets
   `CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y`, `CONFIG_LOG_DEFAULT_LEVEL_NONE=y`,
   `CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL_NONE=y`, and pins the console to UART
   (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`). So a release build's UART0 line carries only the
   binary SIBCP mirror after boot. The ROM's very first line (`ESP-ROM:...`, reset cause) is
   emitted before sdkconfig applies and can only be removed via eFuse — **accepted as-is for
   now** (maintainer decision 2026-05-31): it's one short burst once at power-on, not during
   play.

### Build flavors: release (UART, clean) vs debug (USB, verbose)

| | Release (default) | Debug overlay |
|---|---|---|
| Config | `sdkconfig.defaults` only | `sdkconfig.defaults` + `sdkconfig.debug` |
| IDF/bootloader logs | OFF (`*_LOG_LEVEL_NONE`) | INFO |
| IDF console route | UART0 | **USB-C** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) |
| SIBCP host transports | UART1 IO4/IO5 + USB-C input/output; UART0 TX output mirror | UART1 stays clean; USB-C has debug logs mixed with SIBCP |
| System game-state output | `/system/game_state` SIBCP topic on UART1, USB-C, UART0 TX | same, but USB-C is not clean because debug logs are enabled |
| Built by | CI on every tag | local dev only |

Debug build command:
`idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" build flash monitor`
(later defaults file wins on conflicting choices). This keeps UART0 and UART1 usable for the
robot while a developer watches logs over USB-C, but USB-C should not be treated as a clean
SIBCP transport in that debug flavor.

## Source files reviewed

`RCj_comm_module.ino`, `main/app_main.cpp`, `definitions.h`, `ble.cpp/.h`,
`ble_processing.cpp/.h`, `interbot_comm.cpp/.h`, `sibcp_protocol.cpp/.h`,
`state_machine.cpp/.h`, `display.cpp/.h`, `functions.cpp/.h`, git commit `ca23261`.

## Open questions

- Should the loop adopt a dedicated FreeRTOS task / explicit synchronization for state?
- Is the `.ino` still maintained, or should it be retired in favor of the IDF `main/` path?
</content>
