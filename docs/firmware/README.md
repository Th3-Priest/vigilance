# Flasher firmware

Put the merged image here so the browser flasher can serve it:

```
docs/firmware/vigilance-t-embed-cc1101.bin
```

That file is the build output `.pio/build/lilygo-t-embed-cc1101/firmware.factory.bin`
(a full image that flashes at offset 0x0). Copy it after each build:

```bash
cp .pio/build/lilygo-t-embed-cc1101/firmware.factory.bin docs/firmware/vigilance-t-embed-cc1101.bin
```

The manifest at `docs/manifest.json` points to this path and pins the version.
Bump both the version in the manifest and this binary on every release.
