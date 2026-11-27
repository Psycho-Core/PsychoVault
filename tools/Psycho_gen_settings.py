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
    "downloads": [("Downloads", ["downloadDir", "downloadDirPick", "askSaveLocation", "downloadNotify"])],
    "extensions": [("Proton VPN", ["vpnCheckOpen", "protonVpnRestore", "protonVpnStatus"]),
                   ("Proxy", ["proxySelfTest", "proxyStatus"]),
                   ("Extensions", ["extAddFolder", "extAddCrx", "extReload"])],
    "performance": [("Resource limits", ["cpuLimit", "netLimit"]),
                    ("Sleeping tabs", ["sleepTabsMin", "sleepExclude"]),
                    ("Battery", ["batterySaver", "batteryAuto"])],
    "appearance": [("Appearance", ["theme", "accent", "hideSidebar", "modCss", "modSave", "modClear"]),
                   ("Enhance & personality", ["enhanceVideo", "enhanceImages", "uiSounds", "soundVol", "mediaKeys", "mediaAutoPause", "panicEnabled", "panicUrl"]),
                   ("Sidebar services and speed dial", ["railIcons", "svcAdd", "svcSave", "tileAdd", "tileSave"])],
    "spelling": [("Spell checking", ["spellCheck", "spellBuiltin", "spellLangs", "spellDictUrl"]),
                 ("Personal dictionary", ["spellUserWords", "spellWordsSave", "spellWordsRefresh"])],
    "passwords": [("Vault", ["vaultEnabled", "vaultLockMin", "vpw", "vgo", "vforget", "vnew", "vset", "vLock", "vRefresh"]),
                  ("Backup and import", ["vbackupPassword", "vbackupConfirm", "vBackup", "vRestore", "vImport"]),
                  ("Add or edit an entry", ["eSite", "eUser", "ePass", "eGen", "eSave"])],
    "accessibility": [("Language", ["translateTarget"]),
                      ("Interface comfort", ["highContrast", "reduceMotion", "showTabCloseButtons"])],
    "about": [("Updates", ["updateCheckNow", "updateToken", "updateTokenSave", "updateTokenForget"]),
              ("Troubleshooting", ["openErrorLog", "diagnosticsRefresh", "diagnosticsCopy", "diagnosticsReport"])],
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
        groups = [(t, [i for i in ids if i in by_id]) for t, ids in GROUPS[cid]]
        listed = {i for _, ids in groups for i in ids}
        leftovers = [c["id"] for c in category["controls"] if c["id"] not in listed]
        if leftovers:
            groups.append(("More", leftovers))
        for title, ids in groups:
            rows = group_buttons([render_control(by_id[i], defaults) for i in ids])
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
