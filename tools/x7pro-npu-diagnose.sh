#!/bin/sh
# Read only. Does not install runtimes, load drivers, or read personal media.
set +e
echo '=== CPU architecture / SoC clues ==='
uname -m
if [ -r /proc/device-tree/compatible ]; then
  tr '\000' '\n' < /proc/device-tree/compatible | grep -i -E 'rockchip|rk3562|rk356' | head -10
fi
echo '=== possible NPU nodes ==='
for p in /dev/rknpu /dev/rknn /dev/rknpu0; do
  if [ -e "$p" ]; then ls -l "$p"; else echo "$p: missing"; fi
done
echo '=== RKNN runtime libraries (directory names only; no user files) ==='
for dir in /usr/lib /usr/local/lib /lib /system/lib /system/lib64; do
  if [ -d "$dir" ]; then
    find "$dir" -maxdepth 3 -type f \( -name 'librknnrt.so*' -o -name 'librknnmrt.so*' \) -print 2>/dev/null | head -12
  fi
done
echo '=== installed runtime tooling ==='
for tool in rknn_server rknn_benchmark rknn; do command -v "$tool" || true; done
echo '=== camera / ISP nodes ==='
for p in /dev/video0 /dev/video1 /dev/media0 /dev/rkisp*; do
  if [ -e "$p" ]; then ls -ld "$p"; fi
done
echo '=== no NPU inference attempted by this read-only script ==='
