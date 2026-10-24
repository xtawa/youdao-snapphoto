# SnapPhoto 0.8.0 · PenOS 4.34.0

## Direction

Select automatic direction in camera settings. The firmware's `gsensor_orient`
classifications are 0/1 for landscape and 2/3 for portrait; face-up/down events
retain the last useful direction. A posture must remain stable for 500 ms.
The sensor remains enabled when automatic selfie is disabled. Capture and
recording suspend changes; a recording keeps its initial dimensions/direction.

Preview, photos and MJPEG video rotate their pixels. Portrait photos now retain
the selected photo profile's full resolution with swapped dimensions, rather
than the old 216×720 crop. ARM NEON transposes the YUV planes; RGB filters use a
scalar fallback. The ISP capture format is unchanged.

## System album and media browser

The stock album MiniApp is `8001718963156066`. Its camera-style grid has five
160-pixel columns on the 936×280 display and a vertical scroller. SnapPhoto uses
that verified scroller protocol, with a window of visible tiles and cached
240-pixel thumbnails. Tap a tile to preview, swipe or use arrows for adjacent
media, or choose Select for multi-selection, export and confirmed deletion.

The firmware scanner recognizes uppercase `.JPG`; it skipped SnapPhoto's old
lowercase `.jpg` captures. New photos use `.JPG`. Publication invokes the stock
`scanImages` / `loadImages` protocol and confirms the actual saved path in the
result before returning `indexed:true`. The adapter loads the installed stock
album's own libraries and loader; those proprietary libraries are not copied
into this app or its source archive.

For older JPEGs, select them and choose Import to system album. This changes
the extension to `.JPG` without re-encoding. A conflicting destination is never
overwritten, and failure retains the original. PNGs and videos remain available
in SnapPhoto; the stock album's JPEG indexing is not represented as video support.

Thumbnail workers stop after the first complete JPEG, bound their runtime and
cache size, and never decode full videos just to generate covers. Local video
playback uses the installed GStreamer decoder and audio sink, releases the camera
before playing, and stops its own child process when leaving the preview.

Deletes accept only canonical regular media files directly in Pictures, reject
symlinks and invalid mixed batches before changing any file, invalidate selected
thumbnail entries and refresh the system album. Export shares stop before deletion.

## Automatic low light

Default: Auto / Speed priority. Settings also provide Off and Quality priority.
The camera samples scene luma, uses separate entry/exit thresholds with hysteresis,
and smoothly adjusts a shadow curve. Quality priority adds spatial edge-preserving
luma denoising. Preview, photos and video use the same enhancement policy. No
exposure register, focus motor, ISP daemon, clock or system camera configuration
is modified. This is software enhancement, not multi-frame HDR or added sensor detail.

## Export and verification

Original media download remains unchanged. MP4 previews use ranged inline media.
SnapPhoto MJPEG/PCM AVIs use indexed JPEG frames with a ranged WAV audio stream;
the browser follows the audio clock instead of downloading entire AVIs into RAM.
Preview frame rate is capped at eight updates per second in the browser. Existing
unsupported AVI codecs still have an original-file download.

Native reports distinguish initial validation and the optimized portrait result.
Tests use only newly generated media for deletion. USB-forwarded HTTP validation
does not establish that two Wi-Fi clients can communicate. ADB persistence and
boot configuration were deliberately excluded from this release.

Rotation intrinsics were checked against the [Arm ACLE documentation](https://arm-software.github.io/acle/main/acle.html).
