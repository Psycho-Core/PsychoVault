#!/usr/bin/env python3
"""Generate ui/settings.html from docs/reference/legacy-inventory.json.

The inventory fixes what must exist: every control's id, kind, type, option list and
default. This script adds the layout (cards, rows, switches) in my own markup. Control
labels and descriptions come from the legacy captures where I could read them; where a
capture did not show a control's text (the captures are one viewport tall) the label is
the inventory label, a few of which are wrong in the inventory itself and are corrected in
LABELS below. Grouping into cards beyond the captured viewports is my reconstruction.

  python3 tools/Psycho_gen_settings.py            # write ui/settings.html
  python3 tools/Psycho_gen_settings.py --check    # fail if the file is out of date
"""
from __future__ import annotations
import argparse
import html
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "docs/reference/legacy-inventory.json"
OUTPUT = ROOT / "ui/settings.html"

# id -> (label, description). Labels the inventory gets wrong (it records the nearest label,
# so several textareas and password fields repeat another control's), and copy read from captures.
COPY = {
    "search": ("Search engine", "No sponsored results in any of these."),
    "restore": ("Restore tabs on launch", "Reopens the tabs you had open last time."),
    "workspacesEnabled": ("Workspaces", "Show workspace groups and enable workspace switching. Turning this off keeps your saved groups. Turning it back on puts your current tabs in a new group."),
    "homepage": ("Home page", "What the home button opens."),
    "blockAds": ("Block ads and trackers", "Uses PsychoVault’s built-in domain list, checked on every request. It is not an extension and does not have uBlock Origin’s settings or filters. No remote calls."),
    "remember": ("Remember history", "Turn off and history stops being written. Private windows never record."),
    "clear": ("Clear browsing history", "Bookmarks are kept."),
    "blockUseBundled": ("Use bundled domain list", "This is PsychoVault’s local domain list, not uBlock Origin."),
    "theme": ("Browser theme", "Changes browser and settings colours."),
    "accent": ("Accent colour", "The accent colour, applied to the whole UI. Private windows stay violet."),
    "hideSidebar": ("Hide the sidebar", "Collapses the icon rail for more page width."),
    "enhanceVideo": ("Enhance videos", "Contrast, saturation and brightness lifted on every video element."),
    # Read from the captures (docs/reference/legacy-ui/06-13). Where the capture describes the old
    # engine (Chromium, Electron) the sentence says what my browser does instead.
    "downloadDir": ("Download folder", "Used for automatic downloads and as the starting folder for Save As. Empty means your system Downloads folder."),
    "askSaveLocation": ("Ask where to save each download", "On: choose a name and location each time. Off: save automatically to the download folder."),
    "downloadNotify": ("Notify when a download finishes", "A system notification while PsychoVault is in the background. The rail badge and the in-window message happen either way."),
    "cpuLimit": ("CPU limit", "Lowers this browser's scheduler priority so games and captures get the cores."),
    "batterySaver": ("Battery saver", "Battery saver: lowers priority further and sleeps tabs after three minutes."),
    "sleepTabsMin": ("Sleep tabs after", "Minutes before idle tabs sleep; 0 means never. Pinned tabs never sleep. Battery saver can sleep tabs sooner."),
    "sleepExclude": ("Never sleep these sites", "One hostname per line, for example example.com. Exact hostnames only. Audio-playing and loading tabs are also skipped. Unsaved forms may still be lost."),
    "netLimit": ("Bandwidth cap", "Downloads are paused and resumed to hold the average, so this is a duty cycle, not a hard ceiling."),
    "batteryAuto": ("Battery saver on unplug", "Turn the limiter and fast tab sleep on by itself when the power lead comes out."),
    "spellCheck": ("Spell-check text you type in pages", "Turns the whole feature on or off: red underlines in a page's own fields, the suggestions in the right-click menu and \u201cAdd to dictionary\u201d. The address bar, the palette and this page are deliberately not checked."),
    "spellBuiltin": ("PsychoVault's own checker", "This is what draws the red line and offers \u201cthe\u201d for \u201cteh\u201d: a built-in word list plus your own words, checked in the page. Turn it off only if you do not want any checking."),
    "spellLangs": ("Languages", "Which word list the checker prefers: en uses the English list; any other language has no dictionary here because only English ships. Blank uses your system locale; case does not matter."),
    "spellDictUrl": ("Dictionary source", "A folder or mirror holding <lang>.dic and <lang>.aff Hunspell files. Nothing is fetched from here; the built-in checker is what makes the feature work."),
    "vaultEnabled": ("Vault enabled", "Off means the browser reads nothing from the vault and writes nothing to it."),
    "vaultLockMin": ("Auto-lock after", "Wipes the decrypted vault from memory. Copying a password starts a one-minute lock on its own."),
    "translateTarget": ("Translate pages to", "This is the destination language. The page's original language is detected automatically."),
    "highContrast": ("High-contrast interface", "Uses brighter text, stronger borders, and a clear accent in PsychoVault's controls and Settings."),
    "reduceMotion": ("Reduce motion", "Reduces browser-interface animation and asks websites to minimize transitions and scrolling effects."),
    "showTabCloseButtons": ("Always show tab close buttons", "Keeps each close button visible instead of only showing it on hover or focus."),
    "openErrorLog": ("Error log", "Logs can contain private paths or URLs. Review and redact them before sharing."),
    "updateCheckNow": ("Automatic updates", "Checked quietly once at startup, and only on packaged Windows x64 builds. This runs the same check and tells you the answer even when it fails."),
    # inventory labels that name a neighbour, not the control
    "customSearchEngines": ("Custom search shortcuts", ""),
    "blockAllowSites": ("Never block on these sites", ""),
    "blockCustomDomains": ("Extra blocked domains", ""),
    "modCss": ("Custom stylesheet", ""),
    "spellUserWords": ("Personal dictionary", ""),
    "diagnosticsReport": ("Troubleshooting report", ""),
    "vpw": ("Master password", ""), "vnew": ("New master password", ""),
    "vbackupPassword": ("Backup password", ""), "vbackupConfirm": ("Confirm backup password", ""),
    "eSite": ("Site", ""), "eUser": ("Username", ""), "ePass": ("Password", ""),
}
# textareas: (caption shown above, placeholder)
AREAS = {
    "customSearchEngines": ("Custom search shortcuts - one per line: shortcut | name | HTTPS URL with %s for the search words. Example: gh | GitHub | https://github.com/search?q=%s. Type gh search words in the address bar or new tab.", "gh | GitHub | https://github.com/search?q=%s"),
    "blockAllowSites": ("Never block on these sites - one exact top-level hostname per line. Changes affect future requests; reload the tab.", "example.com"),
    "blockCustomDomains": ("Extra blocked domains - one domain per line. Subdomains are included. This accepts domain names, not uBlock/Adblock Plus filter syntax.", "ads.example.com"),
    "modCss": ("Custom stylesheet - a one-file mod for the browser chrome. CSS only, no shaders, and it cannot reach into pages.", ":root { --accent: #ff0055 }"),
    "sleepExclude": ("Never sleep these sites - one hostname per line.", "example.com"),
    "spellUserWords": ("Words I have added - one per line.", ""),
    "diagnosticsReport": ("A report you can copy when something goes wrong. It contains no page content.", ""),
}
PLACEHOLDERS = {"homepage": "psychovault://newtab", "permOrigin": "https://example.com", "downloadDir": "",
                "dohTemplate": "https://dns.example/dns-query{?dns}", "denyPerms": "", "soundVol": "",
                "panicUrl": "https://example.com", "spellLangs": "en-US", "spellDictUrl": "",
                "updateToken": "", "eSite": "example.com", "eUser": "", "ePass": ""}
DANGER = {"clear"}
# cards: (title, [ids]) per category; ids not listed here still appear, in a final "More" card
GROUPS = {
    "general": [("Search", ["search", "customSearchEngines", "customSearchSave"]),
                ("Startup", ["restore", "workspacesEnabled", "homepage"])],
    "privacy": [("Privacy", ["blockAds", "remember", "clear"]),
                ("Ad-blocker lists and site exceptions", ["blockUseBundled", "blockAllowSites", "blockCustomDomains", "blockSaveLists"]),
                ("Site permissions", ["permOrigin", "permLoad", "permCamera", "permMicrophone", "permLocation", "permNotifications", "permSave", "permRemove"]),
                ("Browser hardening", ["blockThirdPartyCookies", "sendDNT", "blockAutoplay", "webRtc", "doh", "dohTemplate", "stripReferrer", "denyPerms", "relaunch"]),
                ("Containers", ["contAdd", "contSave"]),
                ("Cleanup", ["clearOnExit", "dataRefresh"])],
    "downloads": [("Downloads", ["@downloads_intro", "downloadDir+downloadDirPick", "askSaveLocation", "downloadNotify"])],
    "extensions": [("Extensions", ["@ext_intro", "@ext_callout_open", "vpnCheckOpen", "protonVpnRestore", "protonVpnStatus",
                                   "proxySelfTest", "proxyStatus", "@ext_callout_close", "@ext_after",
                                   "extAddFolder", "extAddCrx", "extReload"])],
    "performance": [("Governor", ["cpuLimit", "batterySaver", "sleepTabsMin", "sleepExclude", "netLimit", "batteryAuto"])],
    "appearance": [("Appearance", ["theme", "accent", "hideSidebar", "modCss", "modSave", "modClear"]),
                   ("Enhance & personality", ["enhanceVideo", "enhanceImages", "uiSounds", "soundVol", "mediaKeys", "mediaAutoPause", "panicEnabled", "panicUrl"]),
                   ("Sidebar services and speed dial", ["railIcons", "svcAdd", "svcSave", "tileAdd", "tileSave"])],
    "spelling": [("Spelling", ["spellCheck", "spellBuiltin", "spellLangs", "spellDictUrl", "@spell_status", "@spell_words",
                               "spellUserWords", "spellWordsSave", "spellWordsRefresh"])],
    "passwords": [("Passwords", ["@pw_status", "vaultEnabled", "vaultLockMin", "@pw_master", "vnew+vset", "vpw", "vgo", "vforget",
                                 "@pw_backup", "vbackupPassword+vbackupConfirm", "vBackup", "vRestore", "vImport", "@pw_fill",
                                 "@pw_empty", "eSite+eUser+ePass+eGen+eSave", "vLock", "vRefresh"])],
    "accessibility": [("Page translation", ["translateTarget", "@translate_note"]),
                      ("Accessibility", ["highContrast", "reduceMotion", "showTabCloseButtons", "@a11y_tip"])],
    "about": [("About", ["openErrorLog", "diagnosticsRefresh+diagnosticsCopy", "diagnosticsReport", "@about_notes",
                         "updateCheckNow", "updateToken", "updateTokenSave", "updateTokenForget"])],
}
# Raw snippets placed between rows. "@name" in a GROUPS list inserts RAW[name]; "+id" after a control id
# puts that control's bare element on the same row (see combo()).
RAW = {
    "downloads_intro": '<div class="note">Choose whether each download asks where to save. When asking is off, files go to the folder below with a unique name; existing files are not replaced. The Save As dialog is provided by your operating system. Browser updates use Downloads separately.</div>',
    "ext_intro": '<div class="note">Extensions are loaded the way a browser\u2019s \u201cload unpacked\u201d works: install an unpacked folder or a <b>.crx</b> file. Web store one-click installation is not supported. Extensions run inside every tab and can read the pages you visit - only install ones you trust, and note that <b>no signature is verified</b> for a crx here.<br><b>What actually works.</b> Content scripts, page changes, request blocking through the webRequest API, storage, messaging, and - through PsychoVault\u2019s own shim - proxy and privacy settings, which is what a browser VPN extension needs to send traffic through a tunnel. What does not exist in this engine: declarativeNetRequest (the MV3 rules engine), native messaging and a toolbar to click. Known incompatible permissions are flagged below, but this is not a guarantee an extension will work.</div>',
    "ext_callout_open": '<div class="callout"><div class="callout-title">Proton VPN</div><p><b>Installed by default.</b> PsychoVault ships a build of Proton\u2019s own browser extension, compiled from Proton\u2019s GPL-3.0 source. It appears in the list below like any other extension - open its window to sign in and connect.</p><p>Sign in with your own Proton account. Use the VPN button on the sidebar to connect or disconnect. A connection indicator alone does not prove there are no DNS or WebRTC leaks.</p>',
    "ext_callout_close": '</div>',
    "ext_after": '<div class="note">If the VPN says connected but your IP does not change, press <b>Check what proxy is in use</b>. It reports what the browser would really do with your traffic, per profile.<br>Press <b>Check install</b> to see what the bundled extension did at startup.</div>',
    "spell_status": '<div class="note" id="spellStatus">spellchecking on | engine: PsychoVault built-in | locale en-US</div>',
    "spell_words": '<div class="note">Your words - one per line. Saving adds what is new and removes what you deleted, so this list is the dictionary.</div>',
    "pw_status": '<div class="note" id="vaultStatus">vault on - locked - 0 entries</div>',
    "pw_master": '<div class="note">The vault is currently encrypted by your operating system. You can optionally set a master password.</div>',
    "pw_backup": '<div class="note"><b>Import and encrypted backups</b> - unlock the vault first. CSV exports contain plaintext passwords: keep the file private and delete it securely when you are finished. Imports and restores merge entries and skip matching site and username; they never replace existing entries. Keep the backup password safe - it cannot be recovered.</div>',
    "pw_fill": '<div class="note">Vault filling is manual, HTTPS-only and limited to the same host (with www treated as equivalent). Cross-origin frames are not filled.</div>',
    "pw_empty": '<div class="note">No entries yet.<br>Add an entry by hand - or right-click a password field on a real site and choose \u201cSave password for this site\u201d.</div>',
    "translate_note": '<div class="note">Choose <b>Menu \u2192 Translate current page</b> or press <b>Ctrl+K</b> and search \u201ctranslate\u201d. Google Translate receives the page URL and fetches its contents; PsychoVault asks first and opens the translated copy in a new tab. This needs internet access and may not work on every site. Do not use it for private or sensitive pages.</div>',
    "a11y_tip": '<div class="note">Keyboard tip: use <b>Ctrl++</b> / <b>Ctrl+-</b> to zoom the current page and the left/right arrow keys while focused on a tab to switch tabs. Keyboard focus is outlined.</div>',
    "about_notes": '<div class="note">If an update fails while Proton VPN is on, disconnect it and retry. PsychoVault does not bypass the VPN automatically. It removes an updater-downloaded installer only after the new version starts and the file is verified unchanged; older untracked installers are kept.</div><div class="note"><span class="strong">PsychoVault</span></div><div class="note">Browser application (this UI, tabs, bookmarks, history, downloads, blocking) and the rendering engine: written for this project.</div><div class="note">Bookmark, history and settings files live in your user data folder.</div><div class="note">Licensed MIT. Third-party components under their own licences - see THIRD-PARTY-NOTICES.md.</div>',
}
# the eight dynamic editors in the inventory, as empty containers in the card they belong to
DYNAMIC = {"privacy": [("site permission editor", "dynSitePermissions", "Site permissions"), ("container list", "dynContainers", "Containers"),
                       ("data usage table", "dynDataUsage", "Cleanup")],
           "extensions": [("extension list", "dynExtensions", "Extensions"), ("proxy switch list", "dynProxySwitch", "Proxy")],
           "appearance": [("sidebar service catalogue and list", "dynServices", "Sidebar services and speed dial"),
                          ("speed dial tiles", "dynTiles", "Sidebar services and speed dial")],
           "passwords": [("vault entry list", "dynVaultEntries", "Add or edit an entry")]}


def esc(text: str) -> str:
    return html.escape(text, quote=True)


# The inventory's extractor cut these defaults at the "//" of the URL, leaving "'psychovault:" and "'https:".
DEFAULT_FIXES = {"homepage": "psychovault://newtab", "panicUrl": ""}


def default_of(defaults: dict, key: str):
    if key in DEFAULT_FIXES:
        return DEFAULT_FIXES[key]
    raw = defaults.get(key)
    if raw is None:
        return None
    raw = raw.strip()
    if raw in ("true", "false"):
        return raw == "true"
    if len(raw) >= 2 and raw[0] == raw[-1] == "'":
        return raw[1:-1]
    return raw


def render_control(control: dict, defaults: dict) -> str:
    cid, kind, ctype = control["id"], control["kind"], control["type"]
    label, desc = COPY.get(cid, (control["label"], ""))
    default = default_of(defaults, cid)
    left = f'<div class="text"><div class="label">{esc(label)}</div>' + (f'<div class="desc">{esc(desc)}</div>' if desc else "") + "</div>"
    if kind == "input" and ctype == "checkbox":
        checked = " checked" if default is True else ""
        return (f'<div class="row">{left}<label class="switch"><input type="checkbox" id="{cid}"{checked}>'
                f'<span class="track"><span class="knob"></span></span></label></div>')
    if kind == "select":
        options = []
        for option in control["options"]:
            chosen = " selected" if default is not None and str(default) == option["value"] else ""
            options.append(f'<option value="{esc(option["value"])}"{chosen}>{esc(option["text"])}</option>')
        return f'<div class="row">{left}<select class="select" id="{cid}">{"".join(options)}</select></div>'
    if kind == "textarea":
        caption, placeholder = AREAS.get(cid, ("", ""))
        note = f'<div class="note">{esc(caption)}</div>' if caption else ""
        return f'{note}<textarea class="area" id="{cid}" placeholder="{esc(placeholder)}"></textarea>'
    if kind == "input":
        input_type = ctype if ctype in ("text", "password", "number") else "text"
        placeholder = PLACEHOLDERS.get(cid, "")
        value = ""
        if isinstance(default, str) and cid not in ("vpw", "vnew", "vbackupPassword", "vbackupConfirm", "ePass"):
            value = f' value="{esc(default)}"'
        return (f'<div class="row">{left}<input class="field" type="{input_type}" id="{cid}"{value}'
                f' placeholder="{esc(placeholder)}"></div>')
    if kind == "button":
        klass = "btn danger" if cid in DANGER else "btn"
        text = "Clear history" if cid == "clear" else control["label"]
        if cid in COPY:  # a labelled action row: text on the left, the button on the right
            return f'<div class="row">{left}<button class="{klass}" id="{cid}" type="button">{esc(text)}</button></div>'
        return f'<button class="{klass}" id="{cid}" type="button">{esc(text)}</button>'
    raise ValueError(f"unhandled control {control}")


def bare(control: dict, defaults: dict) -> str:
    """The control element alone, for a combined row."""
    cid, kind, ctype = control["id"], control["kind"], control["type"]
    default = default_of(defaults, cid)
    if kind == "textarea":
        return f'<textarea class="area" id="{cid}" placeholder="{esc(AREAS.get(cid, ("", ""))[1])}"></textarea>'
    if kind == "input":
        input_type = ctype if ctype in ("text", "password", "number") else "text"
        value = ""
        if isinstance(default, str) and cid not in ("vpw", "vnew", "vbackupPassword", "vbackupConfirm", "ePass"):
            value = f' value="{esc(default)}"'
        return f'<input class="field" type="{input_type}" id="{cid}"{value} placeholder="{esc(PLACEHOLDERS.get(cid, ""))}">'
    if kind == "button":
        return f'<button class="btn" id="{cid}" type="button">{esc(control["label"])}</button>'
    raise ValueError(f"cannot combine {cid}")


def combo(ids: list[str], by_id: dict, defaults: dict) -> str:
    """One row: the first control's text on the left, every listed control on the right. Rows of only inputs
    and buttons (no text in COPY) are plain control lines."""
    first = by_id[ids[0]]
    right = "".join(bare(by_id[i], defaults) for i in ids if not (i == ids[0] and ids[0] in COPY and first["kind"] in ("textarea",) and False))
    if ids[0] in COPY and first["kind"] != "button":
        label, desc = COPY[ids[0]]
        left = f'<div class="text"><div class="label">{esc(label)}</div>' + (f'<div class="desc">{esc(desc)}</div>' if desc else "") + "</div>"
        return f'<div class="row">{left}<div class="ctl">{right}</div></div>'
    if ids[0] in COPY:  # a labelled action row whose buttons sit together on the right
        label, desc = COPY[ids[0]]
        left = f'<div class="text"><div class="label">{esc(label)}</div>' + (f'<div class="desc">{esc(desc)}</div>' if desc else "") + "</div>"
        return f'<div class="row">{left}<div class="ctl">{right}</div></div>'
    return f'<div class="line">{right}</div>'


def group_buttons(rendered: list[str]) -> list[str]:
    """Adjacent bare buttons share one actions row."""
    out, run = [], []
    for item in rendered:
        if item.startswith('<button class="btn'):
            run.append(item)
            continue
        if run:
            out.append('<div class="actions">' + "".join(run) + "</div>")
            run = []
        out.append(item)
    if run:
        out.append('<div class="actions">' + "".join(run) + "</div>")
    return out


def build() -> str:
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    defaults = inventory["defaultSettings"]
    categories = inventory["settingsCategories"]
    nav, panels, seen = [], [], set()
    for index, category in enumerate(categories):
        cid = category["id"]
        by_id = {c["id"]: c for c in category["controls"]}
        active = " active" if index == 0 else ""
        nav.append(f'<button class="cat{active}" type="button" data-cat="{cid}"><span class="num">{index + 1:02d}</span>'
                   f'<span class="name">{esc(category["title"])}</span></button>')
        cards, used = [], set()
        def present(item):
            return item.startswith("@") or all(part in by_id for part in item.split("+"))
        groups = [(t, [i for i in ids if present(i)]) for t, ids in GROUPS[cid]]
        listed = {part for _, ids in groups for i in ids if not i.startswith("@") for part in i.split("+")}
        leftovers = [c["id"] for c in category["controls"] if c["id"] not in listed]
        if leftovers:
            groups.append(("More", leftovers))
        for title, ids in groups:
            rendered, ids_used = [], []
            for item in ids:
                if item.startswith("@"):
                    rendered.append(RAW[item[1:]])
                elif "+" in item:
                    parts = item.split("+")
                    rendered.append(combo(parts, by_id, defaults))
                    ids_used.extend(parts)
                elif item in by_id:
                    rendered.append(render_control(by_id[item], defaults))
                    ids_used.append(item)
            ids = ids_used
            rows = group_buttons(rendered)
            for item_label, dyn_id, card_title in DYNAMIC.get(cid, []):
                if card_title == title:
                    rows.append(f'<div class="dynamic" id="{dyn_id}" data-editor="{esc(item_label)}">{esc(item_label[:1].upper() + item_label[1:])}</div>')
            used.update(ids)
            cards.append(f'<section class="card group"><h3 class="card-title">{esc(title)}</h3>{"".join(rows)}</section>')
        seen |= used
        hidden = "" if index == 0 else ' style="display:none"'
        panels.append(f'<div class="panel" data-panel="{cid}"{hidden}><div class="eyebrow">Settings category</div>'
                      f'<h2>{esc(category["title"])}</h2><p class="lede">{esc(category["description"])}</p>{"".join(cards)}</div>')
    expected = {c["id"] for cat in categories for c in cat["controls"]}
    if seen != expected:
        raise SystemExit(f"controls missing from the page: {sorted(expected - seen)}")
    return TEMPLATE.replace("%NAV%", "".join(nav)).replace("%PANELS%", "".join(panels))


TEMPLATE = """<!doctype html>
<!-- Generated by tools/Psycho_gen_settings.py from docs/reference/legacy-inventory.json. Do not edit by hand. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Settings</title>
<link rel="stylesheet" href="tokens.css">
<link rel="stylesheet" href="components.css">
<style>
.page{width:1160px;margin:0 auto;padding:38px 0 48px}
.page h1{font-size:26px;line-height:30px;font-weight:700}
.byline{margin-top:10px;font-size:13px;color:var(--pv-muted)}
.layout{display:flex;gap:25px;margin-top:22px;align-items:flex-start}
.cats{width:190px;padding:11px;flex:none}
.cats-title{padding:6px 9px 6px;font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase;color:var(--pv-muted)}
.cat{display:flex;align-items:center;gap:14px;width:100%;height:40px;margin:5px 0 0;padding:0 11px;border:1px solid transparent;border-radius:10px;background:transparent;color:var(--pv-muted);font:13.5px/38px var(--pv-font);text-align:left;cursor:pointer}
.cat .num{width:18px;font:700 11px/38px var(--pv-mono);color:var(--pv-accent)}
.cat.active{background:var(--pv-raised);border-color:var(--pv-accent);color:var(--pv-text)}
main{flex:1}
.eyebrow{font-size:11px;font-weight:700;letter-spacing:.12em;text-transform:uppercase;color:var(--pv-muted)}
.panel h2{margin-top:6px;font-size:22px;line-height:28px;font-weight:700}
.lede{margin:6px 0 14px;font-size:13px;color:var(--pv-muted)}
.group{margin-top:16px;padding:20px 20px 18px}
.group .card-title{margin:0 0 15px}
.row .text{flex:1}
.actions{display:flex;gap:8px;margin:7px 0 0}
.ctl{display:flex;gap:8px;align-items:center}
.row .ctl .area{width:440px;min-height:70px}
.row .ctl .field{width:272px}
.line{display:flex;gap:8px;align-items:center;margin:8px 0 0}
.line .field{flex:1}
.callout{margin:12px 0 10px;padding:14px 15px 14px;border:1px solid var(--pv-accent-weak);border-radius:12px;background:#0c1a2c}
.callout-title{font-size:14px;font-weight:700;color:var(--pv-text)}
.callout p{margin:8px 0 0;font-size:12.5px;line-height:19px;color:var(--pv-muted)}
.strong{color:var(--pv-text)}
.dynamic{margin:12px 0 6px;padding:14px;border:1px dashed var(--pv-border);border-radius:10px;font-size:12.5px;color:var(--pv-muted)}
</style>
</head>
<body>
<div class="page">
  <header><h1>Settings</h1><p class="byline">Built by Psycho-Core</p><span class="save-status" id="save-status"></span></header>
  <div class="layout">
    <nav class="cats card" aria-label="Settings categories"><div class="cats-title">Categories</div>%NAV%</nav>
    <main>%PANELS%</main>
  </div>
</div>
<script src="settings.js"></script>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = build()
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != text:
            print("ui/settings.html is out of date; run tools/Psycho_gen_settings.py", file=sys.stderr)
            return 1
        print("ui/settings.html is up to date")
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)}: {len(text)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
