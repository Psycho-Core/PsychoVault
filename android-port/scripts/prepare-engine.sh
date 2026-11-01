#!/usr/bin/env bash
# Prepare an isolated copy of the pinned engine inputs. Does not build or sign an APK.
set -Eeuo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
port_dir="$(cd -- "$script_dir/.." && pwd -P)"
destination="${1:-$HOME/psychovault-android-engine}"
if [[ "$destination" != /* ]]; then echo "Use an absolute Linux path for the engine workspace." >&2; exit 1; fi
if [[ -e "$destination" || -L "$destination" ]]; then echo "Workspace already exists; not overwriting: $destination" >&2; exit 1; fi
parent="$(dirname -- "$destination")"
if [[ ! -d "$parent" ]]; then echo "Workspace parent does not exist: $parent" >&2; exit 1; fi
parent="$(cd -- "$parent" && pwd -P)"
destination="$parent/$(basename -- "$destination")"
repo_root="$(cd -- "$port_dir/.." && pwd -P)"
case "$destination/" in "$repo_root/"*) echo 'Choose a workspace outside the PsychoVault repository.' >&2; exit 1 ;; esac
if ! command -v git >/dev/null || ! command -v python3 >/dev/null || ! command -v node >/dev/null; then echo 'Git, Python 3 and Node.js are required.' >&2; exit 1; fi
stage="$(mktemp -d "$parent/.psychovault-engine.XXXXXXXX")"
cleanup() { if [[ -n "${stage:-}" && -d "$stage" ]]; then rm -rf -- "$stage"; fi; }
trap cleanup EXIT
cp -R "$port_dir/engine/titanium/." "$stage/"
cp -R "$port_dir/engine/psychovault" "$stage/psychovault"
node "$port_dir/scripts/prepare-ui.cjs" "$stage/psychovault/ui"
python3 "$stage/psychovault/prepare_bundled_extensions.py"
python3 "$stage/psychovault/configure_stage.py"
git init -q "$stage/vanadium"
git -C "$stage/vanadium" remote add origin https://github.com/GrapheneOS/Vanadium.git
git -C "$stage/vanadium" fetch --depth 1 origin 0202b7badff99434822ba5e32e0d738792868455
git -C "$stage/vanadium" checkout -q --detach FETCH_HEAD
if [[ "$(git -C "$stage/vanadium" rev-parse HEAD)" != 0202b7badff99434822ba5e32e0d738792868455 ]]; then echo 'Vanadium revision mismatch.' >&2; exit 1; fi
# A second check protects existing paths if one appeared while fetching.
if [[ -e "$destination" || -L "$destination" ]]; then echo "Workspace appeared during preparation; not overwriting: $destination" >&2; exit 1; fi
mv -T -n -- "$stage" "$destination"
if [[ -e "$stage" ]]; then echo "Workspace appeared during preparation; not overwriting: $destination" >&2; exit 1; fi
stage=''
echo "Prepared pinned upstream source in $destination"
echo 'No APK was built. PsychoVault source additions are staged. No APK or device validation is implied.'
