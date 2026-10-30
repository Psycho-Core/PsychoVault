#!/usr/bin/env bash
# Rebuild the versioned PsychoVault Windows installer.
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

VERSION="${VERSION:-$(node -p "require('./app/package.json').version")}"
PAYLOAD="${PAYLOAD:-/var/tmp/payload}"
OUT="${OUT:-$PWD/PsychoVault-Setup-${VERSION}.exe}"
MUI_ICON="${MUI_ICON:-$PWD/assets/psychovault.ico}"
STAGE="${STAGE:-/var/tmp/stage-build}"

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
for f in main.js download-save.js translation.js update-manifest.js update-public-key.pem auto-updater.js \
         preload.js page-preload.js spell-page.js spell-words.txt ui/renderer.js ui/style.css ui/settings.html \
         ext-proxy.js ext-shim.js builtin/proton-vpn/manifest.json builtin/proton-vpn/js/service.js \
         builtin/README.md builtin/proton-vpn/LICENSE; do
  if [ -f "app/$f" ]; then
    cmp -s "$STAGE/resources/app/$f" "app/$f" || { echo "  MISMATCH in staged $f"; exit 1; }
    echo "  ok  $f  $(stat -c %s "app/$f") bytes"
  fi
done

# Keep the installer-facing documents in step with the source checkout rather than
# carrying stale copies forward from a previously staged Electron payload.
cp LICENSE "$STAGE/LICENSE.txt"
cp README.md "$STAGE/README.txt"
cp VERIFICATION.md "$STAGE/VERIFICATION.txt"

echo "[3/4] makensis"
NSIS_TMPDIR="${NSIS_TMPDIR:-/var/tmp/nsitmp}"
mkdir -p "$NSIS_TMPDIR"
TMPDIR="$NSIS_TMPDIR" makensis -V2 -DWANT_UNINSTALLER \
  -DVERSION="$VERSION" -DMUI_ICON="$MUI_ICON" \
  -DPAYLOADDIR="$STAGE" -DOUTFILE="$OUT" installer.nsi
test -s "$OUT" || { echo "no installer produced"; exit 1; }
echo "  $(stat -c %s "$OUT") bytes -> $OUT"

echo "[4/4] verifying what actually went inside"
V=/var/tmp/vfy-$$; mkdir -p "$V"
7z x -y -bso0 -bse0 "$OUT" 'resources/app/*' README.txt LICENSE.txt VERIFICATION.txt -o"$V" > /dev/null 2>&1
for f in resources/app/main.js resources/app/download-save.js resources/app/translation.js \
         resources/app/update-manifest.js resources/app/update-public-key.pem resources/app/auto-updater.js \
         resources/app/spell-page.js resources/app/spell-words.txt resources/app/preload.js \
         resources/app/page-preload.js resources/app/ui/renderer.js resources/app/ui/style.css \
         resources/app/ui/settings.html \
         resources/app/ext-shim.js resources/app/ext-proxy.js \
         resources/app/builtin/README.md resources/app/builtin/proton-vpn/LICENSE \
         resources/app/builtin/proton-vpn/manifest.json resources/app/builtin/proton-vpn/js/service.js \
         resources/app/builtin/proton-vpn/popup.html; do
  rel="${f#resources/app/}"
  found="$V/resources/app/$rel"
  if [ -f "$found" ] && cmp -s "$found" "app/$rel"; then echo "  identical in installer:  $f"
  else echo "  MISSING/DIFFERENT:       $f"; exit 1; fi
done
for spec in 'README.txt:README.md' 'LICENSE.txt:LICENSE' 'VERIFICATION.txt:VERIFICATION.md'; do
  packed="${spec%%:*}"
  source="${spec#*:}"
  if [ -f "$V/$packed" ] && cmp -s "$V/$packed" "$source"; then
    echo "  identical in installer:  $packed"
  else
    echo "  MISSING/DIFFERENT:       $packed"
    exit 1
  fi
done
rm -rf "$V" "$STAGE"
echo "done."
