#!/bin/sh
# Read-only check: no raw logs, serial numbers or user file enumeration.
set +e
echo '=== firmware / architecture ==='
uname -r
uname -m
echo '=== relevant mount types (without device identifiers) ==='
awk '{print $2" "$3}' /proc/mounts | grep -E '^(/userdisk|/data|/usr|/system|/) ' | head -25
echo '=== miniapp cli location ==='
command -v miniapp_cli || true
echo '=== miniapp cli help ==='
miniapp_cli --help 2>&1 | head -45
echo '=== directory presence only ==='
for dir in /userdisk/SnapPhoto /userdisk/Favorite /data/local/tmp; do
  if test -d "$dir"; then echo "$dir exists"; else echo "$dir not available"; fi
done
echo '=== done ==='
