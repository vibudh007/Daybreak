# SPDX-License-Identifier: GPL-3.0-or-later
"""Export an application-only ESP32 image for the Daybreak partition table."""

import os
from pathlib import Path
import re

from SCons.Script import Import  # type: ignore

Import("env")


def export_app(target, source, env):
    project = Path(env.subst("$PROJECT_DIR"))
    firmware = Path(env.subst("$BUILD_DIR")) / "firmware.bin"
    if not firmware.is_file():
        raise RuntimeError(f"Firmware image is missing: {firmware}")

    version = os.environ.get("RELEASE_VERSION")
    if not version:
        entry = (project / "src/rewrite/main.cpp").read_text(encoding="utf-8")
        found = re.search(r'FIRMWARE_VERSION\s*=\s*"([^"]+)"', entry)
        if not found:
            raise RuntimeError("FIRMWARE_VERSION is missing from the application entry point")
        version = found.group(1)

    variant = env.subst("$PIOENV")
    marker = {"release": "R", "debug": "D", "clean": "C"}.get(variant, variant)
    output = project / "bin" / f"Daybreak_v{version}_{marker}_APP.bin"
    output.parent.mkdir(exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    temporary.write_bytes(firmware.read_bytes())
    temporary.replace(output)
    print(f"[app] {output} ({output.stat().st_size} bytes), flash at 0x10000")


env.AddCustomTarget(
    name="app",
    dependencies=["buildprog"],
    actions=[export_app],
    title="Export Daybreak app image",
    description="Write the app-only image without erasing NVS",
)
