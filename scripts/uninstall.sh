#!/bin/bash
set -e

# Uninstaller for brscan5-longpaper-fix

if [ "$EUID" -ne 0 ]; then
    echo "[!] Please run as root (e.g. sudo ./uninstall.sh)" >&2
    exit 1
fi

SANE_LIB_DIR="/usr/lib/sane"
ORIG_LIB="$SANE_LIB_DIR/libsane-brother5.so.1.0.7"
BACKUP_LIB="$SANE_LIB_DIR/libsane-brother5-real.so.1.0.7"

if [ -f "$BACKUP_LIB" ]; then
    echo "==> Restoring original Brother driver from $BACKUP_LIB..."
    cp -fp "$BACKUP_LIB" "$ORIG_LIB"
    rm -f "$BACKUP_LIB" "$SANE_LIB_DIR/libsane-brother5-real.so.1" "$SANE_LIB_DIR/libsane-brother5-real.so"
    echo "==> Original driver restored."
fi

rm -f /usr/lib/libbrother5_longpaper.so
ldconfig

echo "==> brscan5-longpaper-fix removed."
