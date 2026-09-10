# How to publish Vigilance (local checklist)

Nothing here has been pushed. Run these steps yourself when you are ready.
The handle is already set to `Th3-Priest` in README.md, docs/index.html and the
release notes; if you ever change it, find-replace `Th3-Priest` (and the lowercase
`th3-priest` in the Pages URLs) everywhere.

## 1. Create the repo

- On GitHub > Settings > Emails, keep "Keep my email address private" ON and note
  the `NNN+Th3-Priest@users.noreply.github.com` address it gives you.
- Create a new PUBLIC, EMPTY repo named `vigilance` (no README, no license,
  no .gitignore: this repo already has them). Under your account, or under your
  org "Vigilance's Church" if you prefer (then use that org's URL/Pages slug).

## 2. Point the local repo at YOUR fork (not Bruce)

The current `origin` is the upstream Bruce repo. Rename it, then add your fork:

```bash
git remote rename origin upstream
git remote add origin https://github.com/Th3-Priest/vigilance.git
git remote -v   # origin = your fork, upstream = pr3y/Bruce
```

## 3. Commit under the pseudonym (important)

Your Vigilance changes are not committed yet, so they will take whatever identity
you set now. Set it LOCALLY for this repo so your real name and email never appear:

```bash
git config user.name  "Th3-Priest"
git config user.email "NNN+Th3-Priest@users.noreply.github.com"   # your GitHub noreply address
```

(The existing history is all upstream Bruce contributors, which is correct for
attribution and contains none of your info.)

## 4. Refresh the browser-flasher binary

```bash
cp .pio/build/lilygo-t-embed-cc1101/firmware.factory.bin docs/firmware/vigilance-t-embed-cc1101.bin
```

## 5. Review, commit, push

```bash
git status            # confirm no bruce.conf, no personal files (.gitignore covers .pio, bins, notes)
git add -A
git commit -m "Vigilance: defensive surveillance HUD firmware (fork of Bruce)"
git push -u origin main
```

## 6. Turn on the web flasher (GitHub Pages)

Repo > Settings > Pages > Source: `Deploy from a branch`, branch `main`, folder
`/docs`. After a minute the flasher is live at
`https://th3-priest.github.io/vigilance/`.

## 7. Cut the release

- Repo > Releases > Draft a new release, tag `v0.1.0`.
- Attach `docs/firmware/vigilance-t-embed-cc1101.bin` (rename the upload to
  `vigilance-t-embed-cc1101.bin` if needed).
- Paste `RELEASE_NOTES_v0.1.0.md` as the description.

## 8. Make it findable

- Add repo topics: `esp32-s3`, `t-embed`, `cc1101`, `subghz`, `rf`, `ble`,
  `wifi`, `pentest`, `bruce`, `firmware`, `security`.
- Add a short repo description and the Pages URL as the website.
- Record a 15-20 s screen capture of the Sentinel HUD (radar, standby, boot) and
  drop it at `docs/media/hero.gif` so the README shows it.

## Notes

- Pushing includes the full upstream Bruce history (large, but correct: it keeps
  attribution). That is fine and AGPL-friendly.
- The `.bin` under `docs/firmware/` is ~4 MB and lives in the repo so Pages can
  serve it to the flasher. If you prefer a lean repo, host the bin only in the
  Release and point `docs/manifest.json` at the release asset URL instead.
