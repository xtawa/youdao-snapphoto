#!/bin/sh
# Read-only, privacy-aware album directory check for personally authorized X7 Pro.
# Never prints photo filenames or enumerates media contents.
set +e
echo '=== X7 Pro proposed stock gallery path ==='
TARGET='/userdisk/Pictures'
if [ -d "$TARGET" ]; then
  echo "$TARGET exists"
else
  echo "$TARGET is missing or inaccessible"
fi
if [ -w "$TARGET" ]; then
  echo "$TARGET is writable for current shell user"
else
  echo "$TARGET is not writable for current shell user"
fi
echo '=== app fallback directory ==='
if [ -d /userdisk/SnapPhoto ]; then echo '/userdisk/SnapPhoto exists'; else echo '/userdisk/SnapPhoto missing'; fi
echo '=== userdisk mount type ==='
awk '$2=="/userdisk" {print "mount=" $2 ", filesystem=" $3}' /proc/mounts
echo '=== probe completed without changes ==='
