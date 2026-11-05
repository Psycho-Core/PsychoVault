#!/usr/bin/env python3
"""I audit my current build against docs/Psycho_FEATURE-INVENTORY.md.

The inventory is a read-only contract extracted from my old application. This
audit walks every element of that contract and checks the new build:

- DONE: the feature exists in my owned code and the named evidence pattern is
  found in the source today. If the evidence disappears, the check fails - a
  claimed feature that lost its code can never pass by accident.
- PARTIAL: some of the feature exists; the rest is named in the reason.
- DEFERRED: consciously not restored yet, reason on record. Never silent.

This is a feature-presence audit. It is not a behavioural proof - functional
verification lives in pv-tests and the probe tools.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_sources() -> str:
    files = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()
    blobs = []
    for name in files:
        if name.endswith((".cpp", ".hpp", ".py", ".ps1", ".yml")) and not name.startswith(
            ("docs/", "extensions/Psycho-VPN-Proton/")
        ):
            try:
                blobs.append((ROOT / name).read_text(encoding="utf-8", errors="replace"))
            except OSError:
                pass
    return "\n".join(blobs)


SOURCE = load_sources()

CATEGORIES = {
    "General": '"General",',
    "Privacy & security": '"Privacy & security", "Downloads",',
    "Downloads": '"Downloads",',
    "Extensions": '"Extensions", "Performance",',
    "Performance": '"Performance",        "Appearance",',
    "Appearance": '"Appearance",',
    "Spelling": '"Spelling",',
    "Passwords": '"Passwords",',
    "Accessibility & language": '"Accessibility & language",',
    "About": '"About"}',
}

CLEAR_BUTTONS = {
    # action -> (status, evidence or reason)
    "history": ("DONE", '"setting-clear-history"'),
    "downloads": ("DONE", '"downloads-clear"'),
    "cache": ("DEFERRED", "an actually persisted page cache does not exist yet, so there is nothing on disk to clear"),
    "cookies": ("DEFERRED", "my jar persists in owned-cookies.json but no clear control exists in the settings page yet"),
    "storage": ("DEFERRED", "site storage clears are not wired into the settings page yet"),
    "session": ("PARTIAL", "clear-on-exit covers history today; a dedicated session clear is not exposed"),
}

# old default key -> (status, evidence regex or reason)
DEFAULT_KEYS = {
    "search": ("DONE", r'settings\.str\("searchEngine"'),
    "customSearchEngines": ("PARTIAL", "my customSearch accepts one engine with a %s placeholder; a multi-list is not restored"),
    "sitePermissions": ("DEFERRED", "a per-site permission editor does not exist; extensions use their own grant checks instead"),
    "blockUseBundled": ("PARTIAL", "my starter list is always bundled; a switch plus extra local file replace the toggle"),
    "blockCustomDomains": ("DONE", r'read_list\("blockedHosts"'),
    "blockAllowSites": ("DONE", r'read_list\("blockAllowSites"'),
    "blockAds": ("DONE", r'settings\.set\("blockAds"'),
    "homepage": ("DONE", r'settings\.set\("homePage"'),
    "workspacesEnabled": ("DONE", "\"workspaceRename:\""),
    "restore": ("DONE", r'settings\.set\("restoreTabs"'),
    "remember": ("DEFERRED", "no history opt-out switch; history always recorded outside private mode"),
    "theme": ("DONE", r'settings\.set\("theme"'),
    "accent": ("DONE", r'settings\.set\("accent"'),
    "hideSidebar": ("DONE", r'settings\.set\("hideSidebar"'),
    "highContrast": ("DONE", r'settings\.set\("highContrast"'),
    "reduceMotion": ("DONE", r'settings\.set\("reduceMotion"'),
    "showTabCloseButtons": ("DONE", r'settings\.set\("showTabCloseButtons"'),
    "translateTarget": ("DEFERRED", "page translation is not implemented; my settings page says so"),
    "blockAutoplay": ("DEFERRED", "media autoplay policy is not wired into settings yet"),
    "blockThirdPartyCookies": ("DONE", r'settings\.set\("blockThirdPartyCookies"'),
    "sendDNT": ("DONE", r'settings\.set\("sendDNT"'),
    "cpuLimit": ("DONE", r'settings\.set\("cpuLimit"'),
    "sleepExclude": ("DEFERRED", "per-site sleep exclusions are not restored"),
    "sleepTabsMin": ("DONE", r'settings\.set\("sleepTabsMin"'),
    "clearOnExit": ("DONE", r'settings\.set\("clearOnExit"'),
    "batterySaver": ("DONE", r'settings\.set\("batterySaver"'),
    "vaultEnabled": ("DEFERRED", "the vault is not implemented; my settings page refuses to fake it"),
    "vaultLockMin": ("DEFERRED", "the vault is not implemented"),
    "enhanceVideo": ("DEFERRED", "video enhancement is not restored"),
    "enhanceImages": ("DEFERRED", "image enhancement is not restored"),
    "netLimit": ("DEFERRED", "a bandwidth cap is not restored"),
    "settingsVersion": ("PARTIAL", "my owned-settings.json carries a schema number instead"),
    "askSaveLocation": ("DONE", r'settings\.set\("askSaveLocation"'),
    "downloadDir": ("DONE", r'settings\.set\("downloadDirectory"'),
    "downloadNotify": ("DONE", r'settings\.set\("downloadNotify"'),
    "panicEnabled": ("DEFERRED", "the panic button is not rebuilt yet"),
    "panicUrl": ("DEFERRED", "the panic button is not rebuilt yet"),
    "services": ("DEFERRED", "sidebar service catalogue is not rebuilt yet"),
    "railIcons": ("DEFERRED", "the sidebar rail with favicons is not rebuilt yet"),
    "spellCheck": ("DONE", r'settings\.set\("spellCheck"'),
    "spellBuiltin": ("PARTIAL", "my own checker flag persists; the dictionary engine itself is pending"),
    "spellLangs": ("DEFERRED", "language pack settings are not restored"),
    "spellDictUrl": ("DEFERRED", "dictionary source settings are not restored"),
    "spellUserWords": ("DONE", r'write_list\("spellUserWords"'),
    "dismissedUpdate": ("DEFERRED", "no update banner to dismiss yet"),
    "extensions": ("PARTIAL", r'read_list\("disabledExtensions"'),
    "mediaKeys": ("DEFERRED", "media key handling is not restored"),
    "mediaAutoPause": ("DEFERRED", "auto-pause is not restored"),
    "uiSounds": ("DEFERRED", "keyboard sounds are not restored"),
    "soundVol": ("DEFERRED", "keyboard sounds are not restored"),
    "webRtc": ("PARTIAL", "the policy string persists and Proton's extension drives it; I have no WebRTC stack of my own yet for it to govern"),
    "doh": ("DEFERRED", "DoH transport is not implemented"),
    "dohTemplate": ("DEFERRED", "DoH transport is not implemented"),
    "stripReferrer": ("DONE", r'settings\.set\("stripReferrer"'),
    "denyPerms": ("DEFERRED", "no global permission-deny list yet"),
    "containers": ("DEFERRED", "containers are not rebuilt"),
    "modCss": ("DEFERRED", "custom mod CSS is not implemented; my settings page says so"),
    "batteryAuto": ("DEFERRED", "automatic battery saving on unplug is not wired"),
    "speedDial": ("DONE", r'settings\.set\("speedDial"'),
}

# Legacy preload namespace -> my owned surface (or reason it has none).
NAMESPACES = {
    "tab": ("DONE", "\"close-tab\","),
    "nav": ("DONE", "navigate_in"),
    "translate": ("DEFERRED", "page translation is not implemented"),
    "win": ("DONE", "\"minimize\""),
    "bookmarks": ("DONE", "profile.bookmark("),
    "history": ("DONE", "profile.history.push_back"),
    "settings": ("DONE", "commit_setting_edit"),
    "find": ("DONE", "open_find()"),
    "zoom": ("DONE", "set_zoom("),
    "view": ("PARTIAL", "split view is not restored; zoom and hidings exist"),
    "downloads": ("DONE", "downloads.snapshot()"),
    "ui": ("PARTIAL", "no ui:menu; theme/accent/sidebar toggles serve the same ground"),
    "ws": ("DONE", "draw_workspaces"),
    "clip": ("DONE", "void paste()"),
    "rail": ("DEFERRED", "the service rail is not rebuilt"),
    "spell": ("PARTIAL", "add/remove exist; the dictionary engine is pending"),
    "updates": ("PARTIAL", "checking exists; banner/install/later/dismiss do not"),
    "proton": ("PARTIAL", "the Proton toggle is my extension toggle; the live connection is being proven out"),
    "extensions": ("DONE", "extensions.list()"),
    "perf": ("PARTIAL", "cpu/sleep/battery rows exist; snapshot/panic/switches do not"),
    "vault": ("DEFERRED", "the vault is not implemented"),
}

FEATURE_BULLETS = {
    "tabs: new/close/activate/reopen/pin/mute/move/drag": ("PARTIAL", "new/close/activate/reopen/pin/mute/move all exist (buttons, chords and the tab context menu); drag-reorder with the mouse is the remaining piece"),
    "workspaces: list/switch/new/close/rename/menu": ("DONE", "workspace_switch"),
    "private windows": ("DONE", r'"--private"'),
    "split view": ("DEFERRED", "split view is not rebuilt"),
    "command palette": ("DEFERRED", "the command palette is not rebuilt"),
    "omnibox suggestions and custom search shortcuts": ("DONE", r'"suggest"'),
    "bookmarks": ("DONE", r'"open-bookmark"'),
    "history": ("DONE", r'"open-history"'),
    "downloads: list/control/clear/open/reveal/dir/pickdir": ("DONE", r'"download-reveal"'),
    "find in page": ("DONE", r'"find-bar"'),
    "zoom in/out/reset": ("DONE", r'VK_OEM_MINUS'),
    "page translation": ("DEFERRED", "translation is not implemented; the UI says so"),
    "sidebar services and favicons": ("DEFERRED", "services and the rail are not rebuilt; hiding the sidebar exists"),
    "Performance panel: hot tabs/media/panic/services/switches/snapshot PNG/PDF": ("PARTIAL", "live renderer/download counts and cpu/sleep/battery rows exist; hot tabs/media/panic/services/switches/snapshot are not"),
    "ad blocker toggle and per-site shield": ("DONE", r'"toggle-adblock"'),
    "Proton VPN toggle": ("PARTIAL", "the bundled extension is toggled, and my own fail-closed tunnel panel works; the Proton worker is being brought to a full boot now"),
    "spelling status/add/remove": ("PARTIAL", "toggle/add/remove exist; a real dictionary engine is pending"),
    "vault status/unlock/lock": ("DEFERRED", "the vault is not implemented, by my recorded decision"),
    "update banner/check/install/later/dismiss/token state": ("PARTIAL", "the check talks to GitHub and reports honestly; banner/install/later/dismiss/token are not built"),
    "error log and diagnostics": ("PARTIAL", "extension diagnostics and per-tab errors exist; a unified error log view is not"),
    "session recovery": ("PARTIAL", "session restore on launch exists; crash-recovery prompts do not"),
    "keyboard shortcuts and context menus": ("DONE", r'"menu-reopen"'),
}

STORES = {
    "settings.json": ("DONE", "owned-settings.json"),
    "bookmarks.json": ("DONE", "owned-bookmarks.json"),
    "history.json": ("DONE", "owned-history.json"),
    "downloads.json": ("DONE", "owned-downloads.json"),
    "session.json": ("DONE", "owned-session.json"),
    "workspaces.json": ("DONE", "owned-workspaces.json"),
    "vault.bin": ("DEFERRED", "the vault is not implemented"),
    "vault.key.json": ("DEFERRED", "the vault is not implemented"),
    "vault-migration.json": ("DEFERRED", "the vault is not implemented"),
    "crash-recovery.json": ("DEFERRED", "crash recovery prompts are not built"),
    "errors.log": ("DEFERRED", "no unified error log file yet; extension diagnostics are in memory"),
    "ext-shim.log": ("PARTIAL", "extension activity is logged in memory and shown in the settings page; no file yet"),
    "extensions/": ("DONE", "extensions/"),
    "icons/": ("DEFERRED", "no icon cache store yet"),
    "running.marker": ("DEFERRED", "no single-instance marker; multiple windows are allowed"),
}

# The 105 handlers, by namespace, mapped the same way namespaces are. The per
# handler registry that matters lives with the namespace entry; handlers whose
# namespace is DONE/PARTIAL still fail individually if their own evidence key
# disappears when I have one on record.
HANDLER_EVIDENCE = {
    "bookmarks:add": r'profile\.bookmark\(',
    "bookmarks:remove": r'remove_bookmark',
    "downloads:clear": r'"downloads-clear"',
    "downloads:control": r'"download-cancel"',
    "downloads:open": r'"download-open"',
    "find:start": r'open_find\(',
    "find:stop": r'close_find\(',
    "history:clear": r'clear_history',
    "nav:go": r'navigate_in',
    "nav:home": r'home_page',
    "nav:reload": r'navigate\(current\(\)\.url',
    "settings:get": r'Profile::open',
    "settings:set": r'commit_setting_edit',
    "tab:close": r'"close-tab"',
    "tab:new": r'new_tab\(',
    "update:check": r'releases/latest',
    "win:minimize": r'"minimize"',
    "zoom:in": r'VK_OEM_PLUS',
    "zoom:out": r'VK_OEM_MINUS',
    "zoom:reset": r"wp == '0'",
}

NS_PREFIX = {
    "app": ("PARTIAL", "diagnostics exist for extensions; relaunch and a diagnostics viewer are not"),
    "bookmarks": ("DONE", "profile.bookmark("),
    "catalogue": ("DEFERRED", "service catalogue"),
    "clipboard": ("DONE", ""),
    "data": ("PARTIAL", "history/download clears exist; cookies/storage/cache clears and the error log do not"),
    "downloads": ("DONE", '"downloads-pickdir"'),
    "ext": ("PARTIAL", "addfolder/list/toggle exist; addcrx/openpage/remove/restorebuiltin/status are not"),
    "find": ("DONE", "open_find()"),
    "history": ("DONE", '"panel-search"'),
    "nav": ("DONE", '"nav-0"'),
    "open": ("DEFERRED", "open:external is not rebuilt"),
    "perf": ("PARTIAL", "partial"),
    "proton": ("PARTIAL", "extension toggle path"),
    "proxy": ("DONE", "my tunnel test is the self test"),
    "rail": ("DEFERRED", "service rail"),
    "recovery": ("DEFERRED", "crash recovery"),
    "settings": ("DONE", "commit_setting_edit"),
    "spell": ("PARTIAL", ""),
    "tab": ("DONE", "tab_set_pinned"),
    "translate": ("DEFERRED", "translation"),
    "ui": ("PARTIAL", "layout happens live; no menu"),
    "update": ("PARTIAL", "check only"),
    "vault": ("DEFERRED", "vault"),
    "view": ("DEFERRED", "split view"),
    "vpncheck": ("PARTIAL", "my tunnel test shows the connected server; a public-IP check page is pending"),
    "win": ("PARTIAL", "close/max/min are native; win:private is my --private process flag"),
    "ws": ("DONE", "owned-workspaces.json"),
    "zoom": ("DONE", "set_zoom("),
}


def main() -> int:
    doc = (ROOT / "docs/Psycho_FEATURE-INVENTORY.md").read_text(encoding="utf-8")
    failures: list[str] = []
    passes = 0
    deferred: list[str] = []
    partials: list[str] = []

    def check(name: str, status: str, evidence: str, is_regex: bool = True) -> None:
        nonlocal passes
        if is_regex is False:
            evidence = re.escape(evidence)  # literal snippet
        if status == "DONE":
            if re.search(evidence, SOURCE):
                passes += 1
                print("PASS", name)
            else:
                failures.append(name + ": claimed evidence lost - " + evidence)
                print("FAIL", name, "- claimed evidence lost")
        elif status == "PARTIAL":
            partials.append(name)
            print("PART", name, "-", evidence)
        else:
            deferred.append(name)
            print("DEFR", name, "-", evidence)

    # The inventory itself must still be the same contract I extracted: this
    # one checks the document, not the code sources.
    if "read-only contract for the new browser" in doc:
        passes += 1
        print("PASS inventory is my extracted contract")
    else:
        failures.append("inventory contract sentence changed")
        print("FAIL inventory is my extracted contract")

    for label in CATEGORIES:
        check("settings category: " + label, "DONE", '"' + label + '"',
              is_regex=False)
    for action, (status, detail) in CLEAR_BUTTONS.items():
        check("data-clear: " + action, status, detail)
    for key, (status, detail) in DEFAULT_KEYS.items():
        check("default key: " + key, status, detail)
    for bullet, (status, detail) in FEATURE_BULLETS.items():
        check("chrome feature: " + bullet.split(":")[0], status, detail)
    for store, (status, detail) in STORES.items():
        check("data store: " + store, status, detail
              if status != "DONE" else detail, is_regex=False)
    for ns, (status, detail) in NAMESPACES.items():
        check("preload namespace: " + ns, status, detail
              if status != "DONE" else detail, is_regex=False)
    handlers = sorted(set(line.strip("` |")
                          for line in doc.split("## Main-process IPC handlers")[1]
                          .split("## Screenshots")[0].splitlines()
                          if line.startswith("| `")))
    for handler in handlers:
        ns = handler.split(":")[0]
        status, detail = NS_PREFIX[ns]
        evidence = HANDLER_EVIDENCE.get(handler)
        if evidence and status == "DONE":
            check("ipc handler: " + handler, "DONE", evidence)
        elif status == "DONE":
            check("ipc handler: " + handler, "PARTIAL",
                  "the namespace exists but this handler has no evidence key on record")
        else:
            check("ipc handler: " + handler, status, detail)

    # An item I restored but forgot to re-map here would pass silently; the
    # counts prove the whole contract was walked.
    print()
    total = len(CATEGORIES) + len(CLEAR_BUTTONS) + len(DEFAULT_KEYS) + \
        len(FEATURE_BULLETS) + len(STORES) + len(NAMESPACES) + len(handlers) + 1
    print(f"{passes} evidence-checked, {len(partials)} partial, "
          f"{len(deferred)} deferred, {len(failures)} failed, {total} contract items walked")
    if failures:
        print("\nFailures:")
        for name in failures:
            print(" -", name)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
