#!/bin/bash
set -e

# Installer for brscan5-longpaper-fix
# Enables Brother DS-740D, DS-640, DS-940DW, ADS-1200, ADS-1700W etc.
# to scan long paper / receipts up to 1.828 meters (72 inches) without jamming.

if [ "$EUID" -ne 0 ]; then
    echo "[!] Please run as root (e.g. sudo ./install.sh)" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

echo "==> Building brscan5-longpaper shim..."
make clean
make all

SANE_LIB_DIR="/usr/lib/sane"
ORIG_LIB="$SANE_LIB_DIR/libsane-brother5.so.1.0.7"
BACKUP_LIB="$SANE_LIB_DIR/libsane-brother5-real.so.1.0.7"
TARGET_SO="$SCRIPT_DIR/out/libbrother5_longpaper.so"

if [ ! -f "$ORIG_LIB" ]; then
    echo "[!] Error: Brother SANE driver not found at $ORIG_LIB" >&2
    echo "    Please make sure 'brscan5' is installed first." >&2
    exit 1
fi

if [ ! -f "$BACKUP_LIB" ]; then
    echo "==> Backing up original Brother driver to $BACKUP_LIB..."
    cp -p "$ORIG_LIB" "$BACKUP_LIB"
    # Ensure real symlink exists
    ln -sf "libsane-brother5-real.so.1.0.7" "$SANE_LIB_DIR/libsane-brother5-real.so.1"
    ln -sf "libsane-brother5-real.so.1" "$SANE_LIB_DIR/libsane-brother5-real.so"
fi

echo "==> Installing long-paper shim to $ORIG_LIB..."
rm -f "$ORIG_LIB"
install -m 755 "$TARGET_SO" "$ORIG_LIB"
rm -f /usr/lib/libbrother5_longpaper.so
install -m 755 "$TARGET_SO" /usr/lib/libbrother5_longpaper.so

# Ensure ldconfig is run
ldconfig

echo ""
echo "=========================================================="
echo "  [SUCCESS] brscan5 long-paper fix successfully installed!"
echo "=========================================================="
echo "Your Brother scanner now supports continuous scans up to 1.82m."
echo "Hardware reminder: Long paper must be scanned in SIMPLEX mode"
echo "(single-sided) with the straight output path open."
echo ""
