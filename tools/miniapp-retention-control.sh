#!/bin/sh
# X7 Pro / PenOS 4.34.0: stock launcher's explicit whitelist-cleaner flag.
# No package database edits, remounts, daemons, or app uninstall restrictions.
set -eu
flag=/userdata/.disable_app_whitelist_clean
case "${1:-status}" in
  status) ;;
  enable)
    if [ -L "$flag" ]; then echo 'Refusing a symbolic-link flag' >&2; exit 1; fi
    if [ ! -e "$flag" ]; then (umask 077; : > "$flag"); sync; fi
    ;;
  disable)
    if [ -L "$flag" ] || [ -d "$flag" ]; then echo 'Refusing a non-regular flag' >&2; exit 1; fi
    if [ -e "$flag" ]; then rm -f "$flag"; sync; fi
    ;;
  *) echo 'Usage: sh miniapp-retention-control.sh status|enable|disable' >&2; exit 2 ;;
esac
if [ -e "$flag" ]; then
  echo 'Stock whitelist app cleanup: DISABLED (third-party apps retained)'
else
  echo 'Stock whitelist app cleanup: DEFAULT (remote whitelist may remove third-party apps)'
fi
