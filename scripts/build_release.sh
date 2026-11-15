#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
pio_command="$(command -v pio || true)"
if [[ -z "$pio_command" && -x "$HOME/.platformio/penv/bin/pio" ]]; then
    pio_command="$HOME/.platformio/penv/bin/pio"
fi
if [[ -z "$pio_command" ]]; then
    echo "PlatformIO Core was not found. Install it before building." >&2
    exit 1
fi
"$pio_command" run -e release -t clean
"$pio_command" run -e release -t app

app_file=(bin/Daybreak_v*_R_APP.bin)
if [[ ${#app_file[@]} != 1 ]]; then
    echo "Expected one release app image in bin/" >&2
    exit 1
fi
base="${app_file[0]%_APP.bin}"
cp .pio/build/release/bootloader.bin "${base}_BOOTLOADER.bin"
cp .pio/build/release/partitions.bin "${base}_PARTITIONS.bin"
python -m esptool --chip esp32 merge-bin \
    --flash-mode dio --flash-freq 40m --flash-size 4MB \
    -o "${base}_FULL.bin" \
    0x1000 "${base}_BOOTLOADER.bin" \
    0x8000 "${base}_PARTITIONS.bin" \
    0x10000 "${app_file[0]}"
printf 'Release images: %s_{APP,BOOTLOADER,PARTITIONS,FULL}.bin\n' "$base"
