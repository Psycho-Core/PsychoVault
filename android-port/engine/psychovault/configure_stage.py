#!/usr/bin/env python3
"""Configure a copied upstream tree without changing vendored source."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
path = ROOT / '.gclient'
text = path.read_text(encoding='utf-8')
old = """['python3',
              '../extensions/bundle.py',
              '../extensions/dist',
              'titanium',
              'https://github.com/jqssun/android-titanium-extension/releases/latest/download/titanium.crx']"""
new = "['python3', '../psychovault/prepare_bundled_extension.py']"
if text.count(old) != 1:
    raise SystemExit('Upstream extension hook changed; refusing to prepare a potentially unsafe build')
text = text.replace(old, new)
# The hosted build disables PGO, so do not spend disk/time fetching profile data.
pgo_old = "'checkout_pgo_profiles': True,"
pgo_new = "'checkout_pgo_profiles': False,"
if text.count(pgo_old) != 1:
    raise SystemExit('Upstream PGO checkout flag changed; refusing to guess')
text = text.replace(pgo_old, pgo_new)
path.write_text(text, encoding='utf-8')

# Use a dedicated application ID for the restart, rather than impersonating Titanium
# or silently replacing the differently implemented experimental WebView preview.
args_path = ROOT / 'args.gn'
args = args_path.read_text(encoding='utf-8')
old_package = 'chrome_public_manifest_package = "io.github.jqssun.helium"'
if args.count(old_package) != 1:
    raise SystemExit('Upstream Android GN arguments changed; refusing to guess application identity')
args = args.replace(old_package, 'chrome_public_manifest_package = "com.psychocore.psychovault"')
args_path.write_text(args, encoding='utf-8')

# The Chromium additions are installed after Titanium's own source changes.
patch_path = ROOT / 'patch.sh'
patch = patch_path.read_text(encoding='utf-8')
marker = 'python3 "$SCRIPT_DIR/psychovault/install_chromium_source.py" "$PWD" || { echo "PsychoVault source install failed" >&2; exit 1; }'
if marker in patch:
    raise SystemExit('PsychoVault source hook already present')
patch_path.write_text(patch + '\n# PsychoVault native browser operations.\n' + marker + '\n', encoding='utf-8')
