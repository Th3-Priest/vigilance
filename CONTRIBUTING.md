# Contributing to Vigilance

Thanks for wanting to help. Vigilance is a defensive-surveillance firmware for
the LilyGo T-Embed CC1101 Plus, forked from Bruce (AGPL-3.0).

## Ground rules

- Vigilance is for education, research, and authorised security testing only.
  Contributions that exist mainly to harm, defraud, or attack systems you do not
  own will not be merged.
- The project stays AGPL-3.0. By contributing you agree your changes ship under
  AGPL-3.0.
- Credit upstream. If you port code from Bruce, Flipper, or anywhere else, keep
  the original notices and add attribution.

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -e lilygo-t-embed-cc1101
# satellite:
cd satellite && pio run -e esp32-c3
```

The merged image ends up in `.pio/build/<env>/firmware.factory.bin`.

## Style

- Match the surrounding code. Keep comments in English.
- No em-dashes in code or docs. Plain hyphens are fine.
- The UI uses the HUD helpers in `src/core/vig_hud.*`. Reuse them so new screens
  match the look.
- Input on the T-Embed is a rotary encoder plus a back button: rotate to move,
  push to select, back button to go back. Poll `check(EscPress)` often inside
  long loops so back stays responsive.

## Pull requests

- One focused change per PR.
- Say what you tested and on which hardware.
- Make sure it builds. The CI builds both the watch and the satellite.

## Reporting issues

Use the issue templates. Include your board, what you did, what you expected,
and what actually happened.
