# Changelog

All notable changes to Vigilance are listed here. The format loosely follows
Keep a Changelog. Vigilance is AGPL-3.0, a fork of Bruce.

## [Unreleased]

### Added
- Screenshot capture: press the encoder and the back button at the same time to
  save the current HUD screen to `/Vigilance/shots` on the SD card (BMP).
- Bug Sweep: many more camera and NVR signatures, a hint that IP cameras often
  expose RTSP, and it now saves a report of what it found to `/Vigilance/reports`.
- Session reports: Watch Mode also writes a run summary to `/Vigilance/reports`.
- About screen (Config > Vigilance UX): version, credits, and repo link.

## [0.1.0]

First public release.

### Added
- Sentinel HUD: a framebuffer home screen with a live radar, restyled menus, a
  lock-on boot animation, and a standby scene.
- Defensive suite: Bug Sweep (WiFi camera finder), BLE Watch (anti-tracker),
  Tail Watch, deauth detection, Sub-GHz Census, on-device Journal, Guardian Eye,
  Sentinel Pulse, and a Swarm mode over ESP-NOW.
- Swarm satellite firmware for ESP32-C3, with a one-click browser flasher.
- Reliable back-button and encoder navigation on the T-Embed CC1101 Plus.

### Based on
- Bruce (pr3y and contributors) and a Sor3nt merge. See NOTICE.
