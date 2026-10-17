#!/bin/sh
# Read-only X7 Pro diagnostics. Run on your own device via authorized ADB.
# Avoids collecting MAC addresses, serial numbers, and user files.
set +e
echo '=== architecture ==='
uname -m
echo '=== kernel ==='
uname -r
echo '=== platform ==='
grep -Ei 'model|hardware|processor' /proc/cpuinfo | head -15
echo '=== video / media nodes ==='
ls -l /dev/video* /dev/media* /dev/v4l-subdev* 2>/dev/null
echo '=== tools ==='
for name in v4l2-ctl ffmpeg gst-launch-1.0 qcamera; do
  command -v "$name" || true
done
echo '=== camera-related libraries ==='
find /usr/lib /lib -maxdepth 2 -type f 2>/dev/null | grep -Ei 'camera|v4l|rkisp|rkaiq|mpp' | head -60
echo '=== done ==='
