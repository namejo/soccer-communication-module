# 07 — Build, Release, and Flashing

## Build system overview

The project is an **ESP-IDF** application that pulls in **Arduino as an IDF component**
(`espressif/arduino-esp32`). The Arduino `.ino` also exists for local Arduino-IDE use, but
the **CI/release build uses the ESP-IDF path** (`main/app_main.cpp`).

### Target & toolchain

| Item | Value | Source |
|------|-------|--------|
| Chip | `esp32c5` | `CMakeLists.txt` (`set(IDF_TARGET "esp32c5")`), `sdkconfig.defaults` |
| ESP-IDF version | **v5.5.4** | `.github/workflows/firmware-release.yml` |
| Arduino component | `espressif/arduino-esp32` `^3.3.0` | `main/idf_component.yml` |
| BLE stack | **NimBLE** peripheral, GATT server | `sdkconfig.defaults` |
| FreeRTOS tick | 1000 Hz | `sdkconfig.defaults` (`CONFIG_FREERTOS_HZ=1000`) |
| Autostart Arduino | **off** (`CONFIG_AUTOSTART_ARDUINO=n`) — `app_main` calls `initArduino()` manually | `sdkconfig.defaults` |
| Flash | **8 MB**, mode `dio`, freq `80m` | `sdkconfig.defaults` (`CONFIG_ESPTOOLPY_FLASHSIZE_8MB`), `build/flasher_args.json` |
| Partition table | Custom single factory app slot, 6 MB | `partitions.csv`, selected by `sdkconfig.defaults` |

### ESP-IDF component layout

`main/CMakeLists.txt` registers everything as the single `main` component:
- App sources: `app_main.cpp` + `../ble.cpp`, `../ble_processing.cpp`, `../buzzer.cpp`,
  `../display.cpp`, `../functions.cpp`, `../interbot_comm.cpp`, `../serial_status.cpp`,
  `../sibcp_protocol.cpp`, `../status_led.cpp`, `../state_machine.cpp`.
- Vendored libs compiled in: ThingPulse OLED (`OLEDDisplay*.cpp`), `QRcodeDisplay`
  (`frame-v1..v10.c`, `qrcodedisplay.cpp`, `qrencode.c`), `QRcodeOled` (`qrcodeoled.cpp`).
- `REQUIRES espressif__arduino-esp32`, `esp_wifi`, `esp_driver_usb_serial_jtag`.
- `-Wno-error=overloaded-virtual` to tolerate the Arduino BLE class hierarchy warnings.

### Partition table

Wi-Fi/ESP-NOW support increases the app image beyond the ESP-IDF default 1 MB factory
partition. `partitions.csv` keeps the simple no-OTA layout but gives the factory app slot
6 MB:

```csv
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x600000,
```

This relies on the confirmed 8 MB flash fitted to the ESP32-C5 module.

### Local build commands

```sh
cd firmware/RCj_comm_module
idf.py set-target esp32c5     # target is already pinned in CMakeLists/sdkconfig.defaults
idf.py build
# flash over USB-C:
idf.py -p <PORT> flash monitor
```

### Release vs debug build flavors

`idf.py build` uses `sdkconfig.defaults` (the **release** flavor): all bootloader/ESP-IDF/
Arduino-HAL logs are OFF and the console is on UART0, so serial outputs carry only binary
SIBCP frames for robots that read serial. Referee state changes are emitted as the reserved
`/system/game_state` topic instead of plain `PLAY`/`STOP` text. **CI always builds this
flavor.**

For development with logs over the USB-C port, layer the **debug** overlay:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" build flash monitor
```

`sdkconfig.debug` re-enables Info logging and routes the IDF console to USB-Serial-JTAG, so
logs appear over USB-C, which conflicts with using USB-C as a clean SIBCP transport during
development. UART0 and UART1 stay clean. (Delete the generated `sdkconfig` when switching
flavors so the new defaults take effect.) See
[02_firmware_architecture.md](02_firmware_architecture.md#build-flavors-release-uart-clean-vs-debug-usb-verbose).

Arduino-IDE alternative: open `RCj_comm_module.ino`, select an ESP32-C5 board, ensure the
bundled `libraries/` are on the path. (Less tested; CI does not use this.)

> **Local environment note:** `idf.py` may not be on `PATH` in every shell. In this checkout,
> the verified local build used the PlatformIO ESP-IDF package and the ESP-IDF Python venv;
> CI still uses `espressif/esp-idf-ci-action`.

## GitHub Actions release flow

File: `.github/workflows/firmware-release.yml`. Triggered on **tag push** matching
`fw-*` or `v*`.

```
push tag (fw-* | v*)
   │
   ├─ job: build  (ubuntu-latest)
   │   1. checkout (submodules: recursive)
   │   2. espressif/esp-idf-ci-action@v1  (esp_idf_version v5.5.4, target esp32c5)  → idf build
   │   3. esp-idf-ci-action  → python ../../tools/prepare_firmware_release.py
   │         --project-dir . --web-src ../../web/flasher --output-dir ../../dist
   │         --version ${{ github.ref_name }}
   │   4. upload-artifact "firmware-release" (dist/)
   │   5. configure-pages + upload-pages-artifact (dist/)
   │
   ├─ job: release  (needs build)
   │     download artifact → gh release create/upload (clobber if exists)
   │        files: dist/firmware/*, dist/manifest.json, dist/version.json, dist/release-notes.md
   │
   └─ job: deploy-pages  (needs build)
         actions/deploy-pages@v4  → publishes dist/ (web flasher + firmware) to GitHub Pages
```

### Tag naming

- `fw-vX.YZ` (e.g. `fw-v0.95`, `fw-v0.96`) — current convention; also accepts `v*`.
- `github.ref_name` becomes the `--version` and the file-name version (`safe_name()`
  sanitizes it). **Keep `definitions.h` `FW_VERSION_MAJOR/MINOR` in sync with the tag**
  (`definitions.h` is currently 0/97; latest released tag is `fw-v0.96`, so a `fw-v0.97`
  tag should accompany the next release).

## `tools/prepare_firmware_release.py`

Run inside the IDF env after `idf.py build`. Steps:

1. Reads `build/flasher_args.json` (chip, flash settings, flash files + offsets).
2. Copies the static flasher site (`web/flasher/`) into `dist/`, plus the V7 board render
   `.readme_images/3D_PCB1_2026-05-21 v41.png` → `dist/assets/module.png` (referenced by
   `web/flasher/index.html`).
3. Copies build artifacts into `dist/firmware/` with versioned names:
   - `rcj_comm_module-<ver>.bin` (app)
   - `rcj_comm_module-<ver>-bootloader.bin`
   - `rcj_comm_module-<ver>-partition-table.bin`
   - `rcj_comm_module-<ver>-merged.bin` (via `esptool ... merge_bin`, single image)
4. Writes:
   - `manifest.json` — esptool-js/ESP-Web-Tools style; uses the **merged** image at offset
     `0x0`, `chipFamily` derived as `ESP32-C5`, `new_install_prompt_erase: true`.
   - `version.json` — version, chip, `chipFamily`, `releaseUrl`, commit SHA, per-file
     size+sha256.
   - `release-notes.md` — human-readable list of files.

> The web flasher consumes `manifest.json` + `version.json` + the merged `.bin`. The
> individual app/bootloader/partition bins are for `idf.py`/esptool users.

## Web flasher (`web/flasher/`)

Static site (`index.html`, `app.js`, `styles.css`, `.nojekyll`). Uses **esptool-js 0.6.0**
(from unpkg) and the browser **Web Serial API**.

| Constant (`app.js`) | Value |
|---------------------|-------|
| `CHIP_FAMILY` | `"ESP32-C5"` |
| `RESET_MODE` | `"usb_reset"` (USB-CDC bootloader entry) |
| `BAUD_RATE` | `115200` |

Flow:
1. On load, fetch `version.json` + `manifest.json`, display version/chip/size/sha.
2. On "Install firmware": `navigator.serial.requestPort()` → `Transport` → `ESPLoader`.
3. `loader.main("usb_reset")` enters the bootloader via USB; `assertExpectedChip()` checks
   the connected chip == `ESP32-C5`.
4. `writeFlash({ fileArray: [merged@0x0], eraseAll: true, compress: true, ... })` with
   progress reporting.
5. After flashing, `hardResetViaUsbSerial()` toggles **DTR/RTS** to reset the module
   (this is the logic refined in commits `51a724e` / `20db907`).

### Requirements / behavior

- **Desktop Chrome or Edge** required (Web Serial). The UI disables the button and shows
  "Web Serial unavailable" otherwise (`index.html` note + `app.js` guard).
- Connect the module over **USB-C** before starting. Flash is fully erased first
  (`eraseAll: true`, `new_install_prompt_erase: true`).
- The merged image is flashed at offset `0x0`.

## Source files vs generated artifacts

| Source (commit these) | Generated (do NOT commit) |
|-----------------------|---------------------------|
| `web/flasher/*`, `tools/prepare_firmware_release.py`, workflow yml, firmware sources | `dist/**`, `build/**`, `managed_components/**`, `manifest.json`, `version.json`, `release-notes.md`, merged/app/bootloader/partition `.bin` |

## Verification status

- **A full clean rebuild was run and verified (2026-05-31)** with ESP-IDF v5.5.4
  (`~/.espressif/v5.5.4`): `rm sdkconfig && idf.py fullclean && idf.py build` succeeded.
  The stale `sdkconfig` (pre-dating the release-config changes) had to be deleted so the new
  `sdkconfig.defaults` (log-NONE, 8 MB flash) would regenerate — the VSCode Espressif
  extension also reuses an existing `sdkconfig`, so delete it / "Full Clean" there too.
- `build/flasher_args.json` now reports `esp32c5`, **8 MB flash** (matching the chip's JEDEC
- **A fresh verification build was run and verified (2026-06-05)** using a separate
  `build_verify2` directory with the tracked `sdkconfig.defaults`. The generated partition
  table selected `factory` at `0x10000` with size `0x600000`; app image size was
  `0x1695c0`, leaving `0x496a40` bytes (76%) free in the app partition.
- `build/flasher_args.json` reports `esp32c5`, **8 MB flash** (matching the chip's JEDEC
  read), `dio`/`80m`, bootloader@`0x2000`, partition-table@`0x8000`, app@`0x10000`.

## Source files reviewed

`.github/workflows/firmware-release.yml`, `tools/prepare_firmware_release.py`,
`web/flasher/{index.html,app.js}`, `CMakeLists.txt`, `main/CMakeLists.txt`,
`main/idf_component.yml`, `sdkconfig.defaults`, `build/flasher_args.json`, README.

## Open questions

- Is the bundled `libraries/` copy authoritative, or should those become managed components?
- Are submodules actually used? (workflow checks out `submodules: recursive`, but no
  `.gitmodules` was observed — verify.)
</content>
