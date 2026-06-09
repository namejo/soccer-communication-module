# Release Notes Draft: Wi-Fi Communication

## Experimental Robot-To-Robot Communication

This release adds an experimental SIBCP bridge for robot-to-robot communication
between two ESP32-C5 communication modules.

Highlights:

- USB-C and UART1 can carry binary SIBCP frames.
- Valid host frames are forwarded over ESP-NOW on 2.4 GHz channel 1.
- Received ESP-NOW frames are revalidated before being written to USB-C/UART.
- Firmware-owned `/system/...` topics expose referee/game-state information.
- Local `/system/set_led` and `/system/play_melody` services control module
  feedback without forwarding those commands to the peer module.
- Python, C, and C++ wrappers are available for Raspberry Pi robot code.

Important limitation:

ESP-NOW communication is currently broadcast and best-effort. It can drop frames
and is not encrypted or authenticated yet. Use this feature for experiments,
tactics, and non-safety-critical data, not as the only source of critical robot
state.

Recommended checks before match use:

- Run the two-module hardware checklist.
- Test with the referee app connected over BLE.
- Watch BLE diagnostic logs for repeated send failures or drops.
- Keep service call timeouts and retries in robot-side code.
