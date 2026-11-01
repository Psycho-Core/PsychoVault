#!/usr/bin/env bash
# Build one arm64 PsychoVault APK from an already-prepared engine stage.
# This uses Chromium's public development keystore, not a private release key.
set -Eeuo pipefail

STAGE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
NOTES="${PSYCHO_NOTES_DIR:-$STAGE/build-notes}"
mkdir -p "$NOTES"
LOG="$NOTES/build.log"
exec > >(tee -a "$LOG") 2>&1

STATUS=failed
STEP="starting"
finish() {
  local code=$?
  if [[ "$code" -ne 0 ]]; then
    {
      echo "PsychoVault Android build notes"
      echo "Status: failed"
      echo "Failed step: $STEP"
      echo "Exit code: $code"
      echo "No APK was published by this script."
      echo "See build.log for the first actionable compiler/build error."
    } > "$NOTES/Release-Notes-Android.txt"
    tail -n 120 "$LOG" > "$NOTES/failure-tail.txt" || true
  fi
  exit "$code"
}
trap finish EXIT
step() { STEP="$1"; echo; echo "=== $STEP ==="; }

cd "$STAGE"
[[ -f common.sh && -f patch.sh && -f args.gn && -d vanadium && -d psychovault ]] || {
  echo "Run prepare-engine.sh before build-engine.sh." >&2; exit 1;
}
source "$STAGE/common.sh"
SCRIPT_DIR="$STAGE"
export SCRIPT_DIR
export VERSION
VERSION="$(grep -m1 -o '[0-9]\+\(\.[0-9]\+\)\{3\}' vanadium/args.gn)"
[[ "$VERSION" == "156.0.8078.79" ]] || { echo "Unexpected Chromium version: $VERSION" >&2; exit 1; }
export DEBIAN_FRONTEND=noninteractive

step "install host build tools"
sudo apt-get update
sudo apt-get install -y file git curl python3 python3-pil imagemagick librsvg2-bin lsb-release
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y libgcc-s1:i386

step "fetch depot_tools and pinned Chromium source"
if [[ ! -d depot_tools ]]; then
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi
export PATH="$STAGE/depot_tools:$PATH"
mkdir -p chromium/src
cd chromium/src
if [[ ! -d .git ]]; then
  git init -q
  git remote add origin https://chromium.googlesource.com/chromium/src.git
fi
git config --global user.name "PsychoVault CI"
git config --global user.email "ci@psychovault.local"
git fetch --depth 1 origin "+refs/tags/$VERSION:refs/tags/$VERSION"
git checkout --detach "$VERSION"
CHROMIUM_COMMIT="$(git rev-parse HEAD)"
cp "$STAGE/.gclient" ../.gclient

step "apply pinned Vanadium/Titanium source patches"
# Match the upstream build's patch exclusions without editing the vendored copy.
rm -rf "$STAGE"/vanadium/patches/*trichrome-{apk-build-targets,browser-apk-targets}.patch
rm -rf "$STAGE"/vanadium/patches/*{detailed,supported}-language*.patch
rm -rf "$STAGE"/vanadium/patches/*javascript-optimizer-{site-setting,settings-UI}.patch
rm -rf "$STAGE"/vanadium/patches/*component-updates.patch
rm -rf "$STAGE"/vanadium/patches/*{pdf,PDF,for-content-public,toolbar-button,configs-from-config-app,config-app-parsing,new-tab-card,predictive-back}*.patch
replace "$STAGE/vanadium/patches" "VANADIUM" "TITANIUM"
replace "$STAGE/vanadium/patches" "Vanadium" "Titanium"
replace "$STAGE/vanadium/patches" "vanadium" "titanium"
git am --whitespace=nowarn --keep-non-patch "$STAGE"/vanadium/patches/*.patch

step "sync Chromium dependencies and run hooks"
gclient sync -D --no-history --nohooks
gclient runhooks
./build/install-build-deps.sh --no-prompt --no-chromeos-fonts --no-arm --no-nacl

step "apply Titanium and PsychoVault source hooks"
source "$STAGE/patch.sh"

step "configure single-architecture arm64 build"
python3 - <<'PY'
from pathlib import Path
p = Path('out/Default/args.gn')
p.parent.mkdir(parents=True, exist_ok=True)
args = Path('../../args.gn').read_text()
replacements = {
    'target_cpu = "arm"': 'target_cpu = "arm64"',
    'symbol_level = 1': 'symbol_level = 0',
    'use_siso = true': 'use_siso = false',
    'generate_linker_map = true': 'generate_linker_map = false',
}
for old, new in replacements.items():
    if args.count(old) != 1:
        raise SystemExit(f'Expected one GN argument: {old}')
    args = args.replace(old, new)
args += '''
# Keep the hosted build inside the runner's disk/RAM budget.
chrome_pgo_phase = 0
use_thin_lto = false
blink_symbol_level = 0
v8_symbol_level = 0
enable_resource_allowlist_generation = false
android_static_analysis = "off"
'''
p.write_text(args)
PY
gn gen out/Default

step "compile chrome_public_apk"
autoninja -C out/Default -j 4 chrome_public_apk

step "validate APK and write release metadata"
mkdir -p "$STAGE/dist"
APK="$(find out/Default/apks -maxdepth 1 -name 'Chrome*.apk' -print -quit)"
[[ -n "$APK" && -f "$APK" ]] || { echo "No APK was produced." >&2; exit 1; }
cp "$APK" "$STAGE/dist/PsychoVault-$VERSION-arm64-v8a.apk"
APK="$STAGE/dist/PsychoVault-$VERSION-arm64-v8a.apk"
export PATH="$PWD/third_party/jdk/current/bin:$PATH"
export ANDROID_HOME="$PWD/third_party/android_sdk/public"
APKSIGNER="$(find "$ANDROID_HOME/build-tools" -name apksigner -print | sort -V | tail -n 1)"
AAPT2="$(find "$ANDROID_HOME/build-tools" -name aapt2 -print | sort -V | tail -n 1)"
[[ -x "$APKSIGNER" && -x "$AAPT2" ]] || { echo "Android SDK validation tools missing." >&2; exit 1; }
"$APKSIGNER" verify --verbose "$APK" > "$NOTES/apksigner.txt"
"$AAPT2" dump badging "$APK" > "$NOTES/apk-badging.txt"
export APK_PATH="$APK"
export NOTES_PATH="$NOTES"
python3 - <<'PY'
import hashlib, json, os, pathlib, zipfile
apk = pathlib.Path(os.environ['APK_PATH'])
badging = pathlib.Path(os.environ['NOTES_PATH']) / 'apk-badging.txt'
text = badging.read_text()
if "package: name='com.psychocore.psychovault'" not in text:
    raise SystemExit('APK package identity mismatch')
if "native-code: 'arm64-v8a'" not in text:
    raise SystemExit('APK architecture mismatch')
with zipfile.ZipFile(apk) as z:
    bad = z.testzip()
    if bad:
        raise SystemExit(f'Corrupt APK member: {bad}')
    names = set(z.namelist())
    for required in ('AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libchrome.so'):
        if required not in names:
            raise SystemExit(f'APK missing {required}')
sha = hashlib.sha256(apk.read_bytes()).hexdigest()
metadata = {
    'applicationId': 'com.psychocore.psychovault',
    'chromiumVersion': os.environ['VERSION'],
    'chromiumCommit': os.environ['CHROMIUM_COMMIT'],
    'abi': 'arm64-v8a',
    'apk': apk.name,
    'sha256': sha,
    'bytes': apk.stat().st_size,
    'signing': 'Chromium public development keystore; not a private production key',
    'runId': os.environ.get('GITHUB_RUN_ID', ''),
    'runAttempt': os.environ.get('GITHUB_RUN_ATTEMPT', ''),
    'commit': os.environ.get('GITHUB_SHA', ''),
}
(apk.parent / 'build-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
(apk.parent / 'SHA256SUMS').write_text(f'{sha}  {apk.name}\n')
notes = f'''PsychoVault Android {metadata['chromiumVersion']} arm64 preview

I built this APK from the PsychoVault source on GitHub Actions.

 - Engine: Chromium {metadata['chromiumVersion']} with the pinned Vanadium/Titanium Android patches and PsychoVault host/UI changes.
 - Application ID: com.psychocore.psychovault
 - ABI: arm64-v8a only. This first build does not include 32-bit ARM or an AAB.
 - SHA-256: {sha}
 - Signing: Chromium's public development keystore. This is a sideloadable preview, not a privately signed production release. Do not expect it to update an APK signed with a different key.
 - Source commit: {metadata['commit'] or 'local'}
 - Workflow run: {metadata['runId'] or 'local'} attempt {metadata['runAttempt'] or 'local'}

What I verified in CI:
 - The APK was compiled, signed, ZIP-tested, and checked with apksigner/aapt2.
 - The package ID and arm64 native library are present.

What I have not verified:
 - I have not run this APK on a physical Android device.
 - Extension behaviour, Proton VPN behaviour, leak protection, ad blocking, and desktop/Android feature parity still need device testing.
 - Some desktop settings remain unsupported on Android and are not presented as working controls.

This preview is provided as-is, without warranty. Review the repository notices and upstream licences before redistributing it.
'''
(apk.parent / 'Release-Notes-Android.txt').write_text(notes)
manifest = f'''PsychoVault Android corresponding source inputs
Repository commit: {metadata['commit'] or 'local'}
Chromium tag: {metadata['chromiumVersion']}
Chromium commit: {metadata['chromiumCommit']}
Titanium commit: 5e9070a98b48f6b6cb239bf90b8718b91795b7c5
Vanadium commit: 0202b7badff99434822ba5e32e0d738792868455
Build script: android-port/scripts/build-engine.sh (staged as build-engine.sh)
Preparation script: android-port/scripts/prepare-engine.sh
PsychoVault overlay: android-port/engine/psychovault/
Upstream notices: android-port/engine/titanium/ and bundled extension licences

The repository source archive plus the pinned Chromium/Vanadium/Titanium inputs and build scripts identify this build. Chromium, Vanadium, Titanium, and the bundled extensions have their own licences; this file is not a substitute for reviewing those obligations.
'''
(apk.parent / 'engine-source-manifest.txt').write_text(manifest)
print(json.dumps(metadata, indent=2))
PY
cp "$NOTES/apksigner.txt" "$NOTES/apk-badging.txt" "$STAGE/dist/"
STATUS=passed
trap - EXIT
{
  echo "PsychoVault Android build notes"
  echo "Status: passed"
  echo "APK: $APK"
  echo "No device, extension, VPN, leak-protection or blocker behaviour test was run."
} > "$NOTES/Release-Notes-Android.txt"
echo "Built and validated $APK"
