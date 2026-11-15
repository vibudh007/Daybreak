import { ESPLoader, Transport } from "https://unpkg.com/esptool-js@0.7.1/bundle.js";

const button = document.querySelector("#install");
const erase = document.querySelector("#erase");
const status = document.querySelector("#status");
const progress = document.querySelector("#progress");
const log = document.querySelector("#log");

function writeLog(message) {
  log.textContent = (log.textContent + message).slice(-6000);
}

async function firmwareParts() {
  const response = await fetch("./manifest.json", { cache: "no-store" });
  if (!response.ok) throw new Error("Could not load the firmware manifest.");
  const manifest = await response.json();
  const build = manifest.builds.find((entry) => entry.chipFamily === "ESP32");
  if (!build || build.parts.length !== 3) throw new Error("Firmware manifest is incomplete.");
  return Promise.all(build.parts.map(async (part) => {
    const file = await fetch(part.path);
    if (!file.ok) throw new Error(`Could not download ${part.path.split("/").pop()}.`);
    return { address: part.offset, data: new Uint8Array(await file.arrayBuffer()) };
  }));
}

button.addEventListener("click", async () => {
  if (!navigator.serial) {
    status.textContent = "This browser does not support USB serial. Use desktop Chrome or Edge.";
    return;
  }

  let transport;
  button.disabled = true;
  progress.hidden = true;
  log.textContent = "";
  try {
    // The port chooser must run immediately after the click.
    status.textContent = "Choose the CYD's USB serial port.";
    const port = await navigator.serial.requestPort();
    status.textContent = "Downloading firmware…";
    const parts = await firmwareParts();
    const totalBytes = parts.reduce((sum, part) => sum + part.data.length, 0);
    const previousBytes = parts.map((_, index) =>
      parts.slice(0, index).reduce((sum, part) => sum + part.data.length, 0));

    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport,
      baudrate: 115200,
      romBaudrate: 115200,
      terminal: {
        clean() { log.textContent = ""; },
        write(message) { writeLog(message); },
        writeLine(message) { writeLog(message + "\n"); },
      },
    });
    status.textContent = "Connecting to the CYD…";
    const chip = await loader.main();
    if (loader.chip?.CHIP_NAME !== "ESP32")
      throw new Error(`Expected an ESP32 board; found ${chip}.`);

    progress.hidden = false;
    status.textContent = "Installing Daybreak…";
    await loader.writeFlash({
      fileArray: parts,
      flashMode: "dio",
      flashFreq: "40m",
      flashSize: "4MB",
      eraseAll: erase.checked,
      compress: true,
      reportProgress(index, written) {
        progress.value = Math.min(100, Math.round((previousBytes[index] + written) * 100 / totalBytes));
      },
    });
    await loader.after("hard_reset");
    progress.value = 100;
    status.textContent = "Daybreak installed. The board is restarting.";
  } catch (error) {
    status.textContent = error.name === "NotFoundError" ? "No serial port selected." :
      `Installation stopped: ${error.message || error}. Check the log and retry.`;
    writeLog(`\n${error.stack || error}\n`);
  } finally {
    if (transport) {
      try { await transport.disconnect(); } catch { /* Already closed after reset. */ }
    }
    button.disabled = false;
  }
});
