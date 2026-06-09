# Raspberry Pi Setup For USB-C SIBCP

Use this when a Raspberry Pi talks to one or two communication modules over
USB-C.

## Check The Serial Port

Plug in the module and run:

```sh
ls /dev/ttyACM*
```

Common result:

```text
/dev/ttyACM0
```

With two modules connected:

```text
/dev/ttyACM0
/dev/ttyACM1
```

If the ports do not appear, check the USB cable first. Some USB-C cables are
power-only.

## Stable Names For Two Modules

Linux can swap `/dev/ttyACM0` and `/dev/ttyACM1` after reconnects. For a robot,
use stable udev symlinks such as `/dev/ttyRC0` and `/dev/ttyRC1`.

From the repository root, list the currently connected communication modules:

```sh
scripts/setup_rc_udev.sh list
```

Install udev rules for the current physical USB ports:

```sh
scripts/setup_rc_udev.sh install
```

The script matches Espressif USB identity `303a:1001`, model
`USB_JTAG_serial_debug_unit`, and the physical USB port path. It does not match
the module serial number. That means a replacement module still becomes the same
`/dev/ttyRC*` device when plugged into the same Raspberry Pi USB port.

To see the rule before installing it:

```sh
scripts/setup_rc_udev.sh install --dry-run
```

If you intentionally move the modules to different Raspberry Pi USB ports, rerun
the install command so the physical-port mapping is refreshed.

## Serial Permissions

If opening `/dev/ttyACM0` fails with permission denied, add your user to the
serial group and log out/in:

```sh
sudo usermod -aG dialout "$USER"
```

On some distributions the group is `uucp` instead of `dialout`.

## Install The Python Helper

From the repository root:

```sh
python -m pip install -e python/sibcp[serial]
```

Smoke test:

```sh
python -c "import sibcp, rjscm; print('ok')"
```

## Monitor Referee/Game State

```sh
python python/sibcp/examples/monitor_game_state.py --port /dev/ttyACM0
```

When the referee app sends PLAY or STOP, you should see decoded
`/system/game_state` updates.

## Two-Terminal Service Test

Terminal 1, robot/module 2:

```sh
sibcp service serve see_ball --port /dev/ttyACM1 --robot-id 2 --value true
```

Terminal 2, robot/module 1:

```sh
sibcp service call see_ball --port /dev/ttyACM0 --peer-id 2
```

Expected result: the caller returns `true`.

## C/C++ Wrapper Build

```sh
cmake -S c/rjscm -B /tmp/rjscm-build
cmake --build /tmp/rjscm-build
cmake --install /tmp/rjscm-build --prefix /tmp/rjscm-install
```

Compile a simple C program:

```sh
gcc my_robot.c -I/tmp/rjscm-install/include -L/tmp/rjscm-install/lib -lrjscm -o my_robot
```

For C++ projects, prefer CMake:

```cmake
find_package(rjscm CONFIG REQUIRED)
add_executable(my_robot my_robot.cpp)
target_link_libraries(my_robot PRIVATE rjscm::rjscm)
```

## Notes

- USB-C serial uses binary SIBCP frames, not plain text.
- Stable udev names can be installed with `scripts/setup_rc_udev.sh install`;
  then use `/dev/ttyRC0` and `/dev/ttyRC1` instead of `/dev/ttyACM0` and
  `/dev/ttyACM1`.
- UART1 uses 460800 baud, 8N1, 3.3 V logic.
- ESP-NOW bridge diagnostics are visible in the BLE log characteristic.
- ESP-NOW broadcast is still best-effort; retry important service calls in robot
  code if needed.
