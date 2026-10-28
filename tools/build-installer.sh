#!/usr/bin/env bash
# Rebuild PsychoVault-Setup-1.0.0.exe.
#
# The Windows runtime it packages is the staged Electron tree in $PAYLOAD
# (PsychoVault.exe with the icon/version info already patched in, the .pak files,
# locales, the DLLs). Only resources/app is re-staged, from ./app in this repo, so
# a code change rebuilds in about a minute instead of re-downloading Electron.
# If you are building from scratch, unpack electron-v<ver>-win32-x64.zip into
# $PAYLOAD, rename electron.exe to PsychoVault.exe, patch its icon and version info,
# then run this.
set -euo pipefail
cd "$(dirname "$0")/.."

PAYLOAD="${PAYLOAD:-/var/tmp/payload}"
OUT="${OUT:-$PWD/PsychoVault-Setup-1.0.0.exe}"
VERSION="${VERSION:-1.0.0}"
STAGE=/var/tmp/stage-build

test -d "$PAYLOAD"        || { echo "no payload dir at $PAYLOAD"; exit 1; }
test -f "$PAYLOAD/PsychoVault.exe" || { echo "no PsychoVault.exe in $PAYLOAD"; exit 1; }

echo "[1/4] staging $PAYLOAD -> $STAGE"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -a "$PAYLOAD"/. "$STAGE"/
rm -rf "$STAGE/\$PLUGINSDIR"
rm -rf "$STAGE/resources/app" "$STAGE/resources/app.asar"

echo "[2/4] copying app source"
mkdir -p "$STAGE/resources/app"
cp -a app/. "$STAGE/resources/app/"
for f in main.js preload.js page-preload.js spell-page.js spell-words.txt \
         ui/renderer.js ui/style.css ui/settings.html; do
  if [ -f "app/$f" ]; then
    cmp -s "$STAGE/resources/app/$f" "app/$f" || { echo "  MISMATCH in staged $f"; exit 1; }
    echo "  ok  $f  $(stat -c %s "app/$f") bytes"
  fi
done

echo "[3/4] makensis"
mkdir -p /var/tmp/nsitmp
TMPDIR=/var/tmp/nsitmp makensis -V2 -DWANT_UNINSTALLER \
  -DPAYLOADDIR="$STAGE" -DOUTFILE="$OUT" installer.nsi
test -s "$OUT" || { echo "no installer produced"; exit 1; }
echo "  $(stat -c %s "$OUT") bytes -> $OUT"

echo "[4/4] verifying what actually went inside"
V=/var/tmp/vfy-$$; mkdir -p "$V"
7z x -y -bso0 -bse0 "$OUT" 'resources/app/*' -o"$V" > /dev/null 2>&1
for f in resources/app/main.js resources/app/spell-page.js resources/app/spell-words.txt \
         resources/app/preload.js resources/app/page-preload.js resources/app/ui/renderer.js \
         resources/app/ui/style.css resources/app/ui/settings.html; do
  rel="${f#resources/app/}"
  found="$V/resources/app/$rel"
  if [ -f "$found" ] && cmp -s "$found" "app/$rel"; then echo "  identical in installer:  $f"
  else echo "  MISSING/DIFFERENT:       $f"; exit 1; fi
done
rm -rf "$V" "$STAGE"
echo "done."
