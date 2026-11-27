#!/usr/bin/env python3
"""P3/S083: the generated settings page must carry every control in the legacy inventory.

Checks the markup, not behaviour: ids, kinds, input types, select options, defaults, the
ten categories and their navigation. Behaviour (state, effect, persistence) is wired in
later steps and is tracked as present-but-unwired in the plan.
"""
from __future__ import annotations
import json
import pathlib
import re
import subprocess
import sys
import unittest
from html.parser import HTMLParser

ROOT = pathlib.Path(__file__).resolve().parents[1]
INVENTORY = json.loads((ROOT / "docs/reference/legacy-inventory.json").read_text(encoding="utf-8"))
PAGE = (ROOT / "ui/settings.html").read_text(encoding="utf-8")


class Collect(HTMLParser):
    def __init__(self):
        super().__init__()
        self.elements = []          # (tag, attrs)
        self.options = {}           # select id -> [(value, text, selected)]
        self._select = None
        self._option = None
        self.panels = []
        self.navs = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        self.elements.append((tag, attributes))
        if tag == "select":
            self._select = attributes.get("id")
            self.options[self._select] = []
        elif tag == "option" and self._select:
            self._option = [attributes.get("value"), "", "selected" in attributes]
            self.options[self._select].append(self._option)
        if "data-panel" in attributes:
            self.panels.append(attributes["data-panel"])
        if tag == "button" and "data-cat" in attributes:
            self.navs.append(attributes["data-cat"])

    def handle_data(self, data):
        if self._option is not None:
            self._option[1] += data

    def handle_endtag(self, tag):
        if tag == "option":
            self._option = None
        elif tag == "select":
            self._select = None


parsed = Collect()
parsed.feed(PAGE)
BY_ID = {a["id"]: (t, a) for t, a in parsed.elements if "id" in a}


class SettingsPageTests(unittest.TestCase):
    def test_generated_file_is_current(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/Psycho_gen_settings.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ten_categories_each_with_a_panel_and_a_nav_button(self):
        ids = [c["id"] for c in INVENTORY["settingsCategories"]]
        self.assertEqual(parsed.panels, ids)
        self.assertEqual(parsed.navs, ids)
        self.assertEqual(len(ids), 10)

    def test_every_inventory_control_is_present_once_with_its_kind(self):
        seen = []
        for category in INVENTORY["settingsCategories"]:
            for control in category["controls"]:
                cid = control["id"]
                seen.append(cid)
                self.assertIn(cid, BY_ID, f"{cid} is missing")
                tag, attrs = BY_ID[cid]
                if control["kind"] == "input":
                    self.assertEqual(tag, "input", cid)
                    expected = control["type"] if control["type"] in ("checkbox", "text", "password", "number") else "text"
                    self.assertEqual(attrs.get("type"), expected, cid)
                else:
                    self.assertEqual(tag, control["kind"], cid)
        self.assertEqual(len(seen), 109)
        self.assertEqual(len(set(seen)), 109)
        ids_in_page = re.findall(r'\bid="([^"]+)"', PAGE)
        self.assertEqual(len(ids_in_page), len(set(ids_in_page)), "duplicate ids in the page")

    def test_selects_carry_the_inventory_options_and_default(self):
        defaults = INVENTORY["defaultSettings"]
        for category in INVENTORY["settingsCategories"]:
            for control in category["controls"]:
                if control["kind"] != "select":
                    continue
                got = parsed.options[control["id"]]
                self.assertEqual([(v, t.strip()) for v, t, _ in got],
                                 [(o["value"], o["text"]) for o in control["options"]], control["id"])
                raw = defaults.get(control["id"])
                if raw and raw.startswith("'"):
                    chosen = [v for v, _, selected in got if selected]
                    self.assertEqual(chosen, [raw.strip("'")], control["id"])

    def test_switch_defaults_follow_the_inventory(self):
        defaults = INVENTORY["defaultSettings"]
        checked = 0
        for category in INVENTORY["settingsCategories"]:
            for control in category["controls"]:
                if control["kind"] == "input" and control["type"] == "checkbox":
                    _, attrs = BY_ID[control["id"]]
                    want = defaults.get(control["id"], "").strip() == "true"
                    self.assertEqual("checked" in attrs, want, control["id"])
                    checked += 1
        self.assertEqual(checked, 27)

    def test_the_eight_dynamic_editors_have_containers(self):
        containers = [a["data-editor"] for t, a in parsed.elements if "data-editor" in a]
        self.assertEqual(sorted(containers), sorted(INVENTORY["dynamicSettingsEditors"]))
        self.assertEqual(len(containers), 8)


class NewTabPageTests(unittest.TestCase):
    PAGE = (ROOT / "ui/index.html").read_text(encoding="utf-8")

    def test_new_tab_has_search_and_six_tiles_and_the_footer(self):
        self.assertIn('id="address"', self.PAGE)
        self.assertIn("Search DuckDuckGo or enter an address", self.PAGE)
        self.assertEqual(len(re.findall(r'class="tile"', self.PAGE)), 6)
        for name in ("GitHub", "YouTube", "Reddit", "Wikipedia", "Mail", "News"):
            self.assertIn(f">{name}<", self.PAGE)
        self.assertIn("Nothing leaves this machine unless you ask it to.", self.PAGE)

    def test_new_tab_talks_to_the_broker_only_through_psycho_tab_new(self):
        script = (ROOT / "ui/app.js").read_text(encoding="utf-8")
        self.assertIn("psycho.tab.new", script)
        self.assertNotRegex(script, r"psycho\.(nav|settings|vault|proton|extensions)")


if __name__ == "__main__":
    unittest.main()
