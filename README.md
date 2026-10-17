# SnapPhoto — Youdao Dictionary Pen X7 Pro

An independent third-party **Falcon/HaaS MiniApp** with a camera-inspired,
936×280 landscape UI for **X7 Pro first**.

**Status: pre-hardware alpha.** UI, camera state machine, gallery, filters
selection, focus grid, zoom buttons, QR rendering and an AMR build workflow
are implemented. **Actual photo capture, recording, focusing, zooming,
native filters, album storage, and LAN HTTP file serving are not working
on stock X7 Pro until a real firmware-compatible native camera bridge
is implemented.** The app displays explicit unavailable errors rather than
claiming a photo was saved.

## Building

Requires Node.js 18 or 20.

```bash
npm install
npm test
npm run build:prod
```

GitHub Actions: **Build X7 Pro SnapPhoto AMR**. Successful runs publish
`SnapPhoto-X7Pro-AMR-UNVERIFIED` with the generated `.amr` package.
The UNVERIFIED suffix describes missing physical device validation.

For an authorized developer device with ADB enabled:

```bash
adb push all.amr /userdisk/Favorite/SnapPhoto.amr
adb shell "miniapp_cli install /userdisk/Favorite/SnapPhoto.amr"
```

Replace `all.amr` with the actual artifact filename when different.

## Architecture

- `src/pages/index/`: X7 Pro-first viewfinder, photo/video, gallery and export UI.
- `src/platform/camera-bridge.js`: explicit native capability detection and lifecycle.
- `src/utils/qr-png.js`: standalone PNG QR renderer.
- `docs/native-bridge.md`: required native functionality and safety contract.
- `docs/x7pro-validation.md`: hardware acceptance checklist.

Reference projects: [Doge Calculator](https://github.com/adogecheems/doge-calculator)
(MiniApp packaging), [PenTool](https://github.com/penosext/miniapp)
(native device toolchains; no GPL-licensed native source reused).
Not affiliated with Youdao. License: MIT.
