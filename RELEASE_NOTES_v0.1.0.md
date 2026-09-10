# Vigilance v0.1.0

First public release. A defensive surveillance HUD firmware for the LilyGo
T-Embed CC1101 Plus, forked from [Bruce](https://github.com/pr3y/Bruce).

## Highlights

- **Sentinel HUD**: a full-screen framebuffer interface with anti-aliased
  primitives. Live-radar home screen that passively scans nearby WiFi, restyled
  menus, a lock-on boot animation, and a standby / screensaver scene (a sentinel
  eye inside a radar that reacts to the threat level).
- **Defensive toolkit**: Bug Sweep (hidden-camera finder), BLE anti-tracker
  Watch, Tail Watch, WiFi deauth detection, Sub-GHz Census, on-device Journal,
  Guardian Eye / Sentinel Pulse, and a Swarm (ESP-NOW) mode.
- Inherits Bruce's full RF, NFC/RFID, IR, 2.4 GHz, WiFi, BLE, GPS, scripting and
  web UI toolset.
- Reliable back-button and encoder navigation on the T-Embed CC1101 Plus.

## Install

**Flash from your browser (easiest):** https://th3-priest.github.io/vigilance/
Chrome / Edge / Brave / Opera on desktop, plug in over USB-C, click Flash.

**Manual (esptool):**

```bash
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash 0x0 vigilance-t-embed-cc1101.bin
```

The `.bin` attached to this release is a merged image; flash it at offset `0x0`.

## Board

Built and tested for the LilyGo T-Embed CC1101 Plus (ESP32-S3, 16 MB flash,
8 MB PSRAM). Other Bruce boards may build but the HUD is tuned for this one.

## Notes and limitations

- The home-screen radar shows live WiFi. BLE and Sub-GHz counts populate once you
  run BLE Watch or Sub-GHz Census (they need exclusive access to the radio).
- Some saved file headers keep the `Filetype: Bruce ... File` marker on purpose,
  for interoperability with Flipper and other tools.

## Credits

Based on Bruce by pr3y and contributors (AGPL-3.0), with work merged from Sor3nt.
See NOTICE and THIRD_PARTY.md. Vigilance is released under AGPL-3.0.

## Full disclosure

Vigilance can transmit and probe radio and network protocols. Use it only on
hardware, networks and radios you own or are authorised to test.
