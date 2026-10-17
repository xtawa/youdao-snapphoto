# X7 Pro native camera interface contract

**Current state: no native camera module has been verified or implemented.**
The MiniApp UI does not pretend that unimplemented camera features work.

An X7 Pro-specific Falcon JSAPI module must register one of:
- `globalThis.SnapPhotoNative`
- `$falcon.jsapi.snapphoto`

See [src/platform/camera-bridge.js](../src/platform/camera-bridge.js).

Expected asynchronous methods:
- `getCapabilities()`: object with camera/photo/video/preview/zoom/focus/gallery/share booleans, supported filters list, minZoom and maxZoom.
- `openPreview()`, `getPreviewFrame()` returning `{image:'data:image/jpeg;base64,...'}` and `closePreview()`.
- `capturePhoto({filter,zoom})` returns `{path}`.
- `startRecording({filter,zoom})` and `stopRecording()` returns `{path}`.
- `setZoom(value)`, `focusAt(normalizedX,normalizedY)`, `setFilter(id)`.
- `listMedia()` returns array of `{path,name,size,type}`, type being photo or video.
- `startShare(paths)` returns `{url}`; `stopShare()` tears down HTTP sharing.

### Native obligations

1. The original 8 MP X7 Pro camera is a hardware resource shared with
   Youdao applications. Only open if free, and release on background/exit.
2. Do not return a capability true before the relevant method actually works.
3. Save to a dedicated allowlisted app folder, such as `/userdisk/SnapPhoto`.
4. Recheck canonical filesystem paths, block symlinks/path traversal, and
   serve only explicitly user-selected SnapPhoto files.
5. Use a cryptographically random time-limited download token. Bind only to
   the trusted LAN interface, not a public Internet-facing endpoint.
6. The current URL is plain HTTP and not encrypted, so use private Wi-Fi.
7. Close recording, microphone and file-sharing server during backgrounding.
8. Test autofocus, video encoder/muxer, and image processing before asserting
   feature support on a specific firmware version.
9. A 2-FPS JPEG base64 preview is a fallback; a native preview overlay is
   preferable for performance.

### Probe before implementation

On the **owner-authorized** device, read-only diagnostics:
```sh
uname -m
cat /proc/cpuinfo | head -30
ls -l /dev/video* /dev/media* 2>/dev/null
which v4l2-ctl ffmpeg gst-launch-1.0 2>/dev/null
ls /usr/lib 2>/dev/null | grep -Ei 'camera|v4l|rkisp|mpp'
```
Do not overwrite the firmware or assume a generic RK3562 board has identical drivers.
