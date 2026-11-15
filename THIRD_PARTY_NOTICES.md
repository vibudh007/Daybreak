# Third-party components

PlatformIO downloads the dependencies pinned in `platformio.ini`. Their code and, where applicable, fonts are included in the firmware image. Daybreak's GPL license covers Daybreak's own source; these components retain their licenses.

| Component | Version used | License and source |
|---|---|---|
| TFT_eSPI, including bundled GFX fonts | 2.5.43 | [`LICENSES/TFT_eSPI.txt`](LICENSES/TFT_eSPI.txt), [`LICENSES/TFT_eSPI-GFXFF.txt`](LICENSES/TFT_eSPI-GFXFF.txt), [source](https://github.com/Bodmer/TFT_eSPI/tree/2.5.43) |
| ArduinoJson | 7.4.2 | [`LICENSES/ArduinoJson.txt`](LICENSES/ArduinoJson.txt), [source](https://github.com/bblanchon/ArduinoJson/tree/v7.4.2) |
| XPT2046_Touchscreen | `5ccb951d` | [`LICENSES/XPT2046_Touchscreen.txt`](LICENSES/XPT2046_Touchscreen.txt), [source](https://github.com/PaulStoffregen/XPT2046_Touchscreen/tree/5ccb951d346017005b1fff7289bf19a1904e2de3) |
| Arduino-ESP32 framework | 3.3.2 | LGPL-2.1-or-later package metadata; [`LICENSES/Arduino-ESP32-LGPL-2.1.txt`](LICENSES/Arduino-ESP32-LGPL-2.1.txt), [source](https://github.com/espressif/arduino-esp32/tree/3.3.2) |
| Arduino-ESP32 bundled ESP-IDF libraries | `5.5.0+sha.07e9bf4970` | Package metadata lists LGPL-2.1-or-later; [build sources](https://github.com/espressif/esp32-arduino-lib-builder). Individual ESP-IDF components may carry additional licenses; consult the corresponding component sources and notices in the pinned package. |

These notices accompany the released binaries. For the complete license terms of a dependency, consult its pinned source and package metadata.

The browser installer loads [esptool-js 0.7.1](https://github.com/espressif/esptool-js/releases/tag/v0.7.1) from a CDN. It is licensed under Apache-2.0.
