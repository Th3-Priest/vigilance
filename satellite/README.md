# Vigilance Satellite

A tiny ESP-NOW firmware for a cheap ESP32 board acting as a node of the
**Vigilance swarm**. The T-Embed CC1101 (the Vigilance master) discovers it and
coordinates it.

## What it does

- Boots as a WiFi station on the swarm **control channel** (channel 1).
- Waits for the master's pairing beacon, announces itself (`HELLO`), then gets
  enrolled (`ENROLL`).
- Sends a **heartbeat** to the master every couple of seconds.
- Takes a **role** (WiFi / BLE / Sub-GHz / LoRa / any) and runs **remote tasks**
  on command (scan, passive watch, measure), reporting results back.
- Supports **OTA** updates pushed from the master.
- **LED**: blinks while looking for a master, solid once enrolled.

## Targets

- `esp32-c3` (default) - e.g. an ESP32-C3 SuperMini, ~1-2 EUR.
- `esp32` - classic ESP32 devkit.
- `esp32-s3` - ESP32-S3 devkit.

## Flash

Easiest: use the browser flasher at https://th3-priest.github.io/vigilance/
("Flash a satellite"). Or build and upload yourself:

```
pio run -e esp32-c3 -t upload
```

One USB flash to start; after that the satellite boots and joins the swarm on
its own.

## Important

- The protocol header (`include/swarm_protocol.h`) **must stay byte-identical**
  to `src/modules/others/swarm.h` in the Vigilance master. A change on one side
  must be mirrored on the other.
- Needs the **Arduino-ESP32 3.x** core (ESP-IDF 5.x) for the modern ESP-NOW API
  (`esp_now_recv_info_t`). If your PlatformIO pulls a 2.x core, adjust the
  `platform` line in `platformio.ini`.
- The LED is assumed on `GPIO8`, active-low (the C3 SuperMini case). Adjust
  `LED_BUILTIN` / `LED_ON` in `src/main.cpp` if your board differs.
