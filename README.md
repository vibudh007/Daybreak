# Daybreak

Daybreak turns the CYD, a 2.8-inch ESP32-2432S028 touchscreen, into a dashboard for the your home. It shows the time and weather, German transport departures, and Home Assistant toggles.

## Features

- Digital or analog clock, date, moon phase, and hourly and daily weather
- Two direction-separated lists for a saved public transport stop, with live times and disruption notes
- A scrollable Home Assistant page for chosen lights and other toggles
- Light and dark themes, brightness control, and configurable page order

Daybreak is designed for the 2.8-inch, 240 × 320 ILI9341 ESP32-2432S028 based CYD. Other CYD variants may need changes before flashing. 2.4 GHz Wi-Fi is required for live data.

## Install

### Option 1: Install in your browser

1. Connect the board to your computer with a data-capable USB cable.
2. Open the [Daybreak web installer](https://vibudh007.github.io/Daybreak/) in desktop Chrome or Edge.
3. Press **Install Daybreak**, select the board's serial port, and follow the prompts.
4. Follow the Wi-Fi setup on the screen after the board restarts.

The installer uses ESP Web Tools and the firmware files from the [current release](https://github.com/vibudh007/Daybreak/releases/tag/v3.0.10-dev). It can offer to erase the device during installation. Erasing removes saved Wi-Fi and app settings; keep them when updating an existing Daybreak installation.

### Option 2: Install locally

1. Install [Python 3](https://www.python.org/downloads/) and esptool:

   ```sh
   python -m pip install esptool
   ```

2. Download the `_FULL.bin` file from the [current release](https://github.com/vibudh007/Daybreak/releases/tag/v3.0.10-dev).
3. Connect the board and find its serial port: usually `/dev/ttyUSB0` on Linux or `COM5` on Windows. Replace `PORT` below with your port, and replace `FIRMWARE.bin` with the downloaded file name:

   ```sh
   python -m esptool --chip esp32 --port PORT write-flash 0x0 FIRMWARE.bin
   ```

4. Restart the board and complete the on-screen Wi-Fi setup.

The full image is for a fresh installation and clears saved app settings. To update an existing Daybreak installation while keeping settings, download the `_APP.bin` file instead and flash it at `0x10000`:

```sh
python -m esptool --chip esp32 --port PORT write-flash 0x10000 Daybreak_v3.0.10-dev_R_APP.bin
```

## Build from source

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) and esptool as shown above, then run:

```sh
./scripts/build_release.sh
```

The script builds the firmware and places the app, bootloader, partition table, and combined full image in `bin/`. The `bin/` folder is generated locally and is not part of the source repository.

## License

Daybreak is licensed under [GPL-3.0-or-later](LICENSE). Downloaded build dependencies retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
