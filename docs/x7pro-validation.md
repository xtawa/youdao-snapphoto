# X7 Pro acceptance checklist

Target: **Youdao Dictionary Pen X7 Pro**, 4.1-inch approximately 936×280
landscape display, 8 MP camera, 64 GB storage (public product reports).

- [ ] Confirm firmware version and CPU architecture on the actual device.
- [ ] Verify physical camera nodes and native userland camera API.
- [ ] Build and load an X7 Pro compatible native Falcon camera JSAPI.
- [ ] Confirm the AMR installs with `miniapp_cli install`.
- [ ] Preview frame rate and no conflict with official camera app.
- [ ] Still photos are valid JPEG/PNG and persist after reboot.
- [ ] Record a playable video with valid timestamps, stop on suspend.
- [ ] Autofocus and zoom alter saved images.
- [ ] Filters apply to full-resolution stills and video as advertised.
- [ ] Album only shows SnapPhoto media, multi-select matches download list.
- [ ] QR connects via the same private network with expiring link.
- [ ] Test offline, low storage, no camera access, rapid app switching.

Use `npm test` for host-side contract tests. Those tests do not certify hardware.

References:
- https://www.ithome.com/0/789/414.htm
- https://github.com/orgs/PenUniverse/discussions/277
- https://github.com/penosext/miniapp
