from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1] / 'engine' / 'psychovault'
INSTALL = ROOT / 'install_chromium_source.py'


class ChromiumSourceTests(unittest.TestCase):
    def make_tree(self, path):
        java = path / 'chrome/android/java/src/org/chromium/chrome/browser'
        java.mkdir(parents=True)
        activity = java / 'ChromeTabbedActivity.java'
        activity.write_text('public class ChromeTabbedActivity extends ChromeActivity implements PreAttachIntentObserver {\n}\nvoid onResumeWithNative() {\n        super.onResumeWithNative();\n}\n    public void onPauseWithNative() {\n}\n    public void onDestroyInternal() {\n}\n    public void onActivityResult(int requestCode, int resultCode, Intent data) {\n            super.onActivityResult(requestCode, resultCode, data);\n}\n')
        gn = path / 'chrome/android/chrome_java_sources.gni'
        gn.write_text('chrome_java_sources = [\n  "java/src/org/chromium/chrome/browser/ChromeTabbedActivity.java",\n]\n')
        (path / 'chrome/android/BUILD.gn').write_text('  android_library("chrome_java") {\n    deps = [\n    ]\n    srcjar_deps += [ ":chrome_jni_headers" ]\n}\n  chrome_public_apk_or_module_tmpl("chrome_public_apk") {\n    target_type = "android_apk"\n}\n')
        ui = path / 'chrome/browser/ui'; ui.mkdir(parents=True)
        (ui / 'BUILD.gn').write_text('static_library("ui") {\n  public_deps = [\n    # WARNING WARNING WARNING\n  ]\n  deps = [\n    ":browser_element_identifiers",\n  ]\n}\n')
        webui = ui / 'webui'; webui.mkdir()
        (webui / 'chrome_web_ui_configs.cc').write_text('#include "chrome/browser/ui/webui/chrome_web_ui_configs.h"\nvoid RegisterChromeWebUIConfigs() {\n  auto& map = content::WebUIConfigMap::GetInstance();\n}\n')
        content = path / 'chrome/browser/chrome_content_browser_client.cc'
        content.write_text('#include "chrome/browser/chrome_content_browser_client.h"\nvoid f() {\n  DCHECK(browser_context);\n  Profile* profile = Profile::FromBrowserContext(browser_context);\n  DCHECK(profile);\n\n  ChromeNavigationUIData* chrome_navigation_ui_data =\n      nullptr;\n}\n')
        spell = path / 'chrome/browser/spellchecker'; spell.mkdir(parents=True)
        (spell / 'spellcheck_hunspell_dictionary.h').write_text('  // Tests use this method to set a custom URL for downloading dictionaries.\n  static void SetDownloadURLForTesting(const GURL url);\n')
        (spell / 'spellcheck_hunspell_dictionary.cc').write_text('GURL& GetDownloadUrlForTesting() {\n  static base::NoDestructor<GURL> download_url_for_testing;\n  return *download_url_for_testing;\n}\nvoid SpellcheckHunspellDictionary::SetDownloadURLForTesting(const GURL url) {\n  GetDownloadUrlForTesting() = url;\n}\nGURL SpellcheckHunspellDictionary::GetDictionaryURL() {\n  if (GetDownloadUrlForTesting() != GURL()) {\n    return GetDownloadUrlForTesting();\n  }\n}\n')
        return java, activity, gn

    def test_controller_is_added_to_activity_and_gn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            java, activity, gn = self.make_tree(root)
            env = dict(__import__('os').environ, PYTHONPATH=str(ROOT.parent))
            # Exercise real installer logic against a temporary copy with generated UI fixtures.
            staged = root / 'staged'; shutil.copytree(ROOT, staged)
            (staged / 'ui').mkdir()
            for name in ('index.html', 'newtab.js', 'settings.js'): (staged / 'ui' / name).write_text('fixture')
            (staged / 'bundled').mkdir(exist_ok=True)
            for name in ('proton-vpn.zip', 'dark-reader.zip', 'blocklist.json'): (staged / 'bundled' / name).write_bytes(b'fixture')
            subprocess.run(['python3', str(staged / 'install_chromium_source.py'), str(root)], check=True)
            self.assertIn('ensurePsychoChromeHost()', activity.read_text())
            self.assertIn('destroyPsychoChromeHost()', activity.read_text())
            self.assertTrue((root / 'psychovault/native/psycho_ui.cc').exists())
            self.assertIn('RegisterPsychoWebUIConfigs', (root / 'chrome/browser/ui/webui/chrome_web_ui_configs.cc').read_text())
            self.assertIn('PsychoTabController.java', gn.read_text())
            self.assertIn('PsychoProfileStore.java', gn.read_text())
            self.assertIn('PsychoExtensions.java', gn.read_text())
            self.assertIn('PsychoVaultStore.java', gn.read_text())
            self.assertIn('handleActivityResult', activity.read_text())
            self.assertIn('CreateNetworkThrottle', (root / 'chrome/browser/chrome_content_browser_client.cc').read_text())
            chrome_gn = (root / 'chrome/android/BUILD.gn').read_text()
            self.assertIn('asset_deps', chrome_gn)
            self.assertIn('//psychovault:bundled_extensions_assets', chrome_gn)
            self.assertNotIn('//psychovault:jni_headers_java', chrome_gn)
            ui_gn = (root / 'chrome/browser/ui/BUILD.gn').read_text()
            self.assertIn('//psychovault/native/psycho_ui.cc', ui_gn)
            self.assertIn('//psychovault:jni_headers', ui_gn)
            self.assertIn('//chrome/browser/spellchecker', ui_gn)
            self.assertNotIn('//psychovault:native', ui_gn)
            self.assertIn('SetDownloadURLMirror', (root / 'chrome/browser/spellchecker/spellcheck_hunspell_dictionary.h').read_text())
            self.assertIn('GetDownloadUrlMirror', (root / 'chrome/browser/spellchecker/spellcheck_hunspell_dictionary.cc').read_text())
            self.assertIn('PsychoContainers.java', gn.read_text())
            self.assertIn('PsychoMedia.java', gn.read_text())
            self.assertIn('PsychoUpdates.java', gn.read_text())
            self.assertEqual((java / 'PsychoTabController.java').read_bytes(), (ROOT / 'java/org/chromium/chrome/browser/PsychoTabController.java').read_bytes())
            old = activity.read_bytes()
            result = subprocess.run(['python3', str(staged / 'install_chromium_source.py'), str(root)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(activity.read_bytes(), old)

    def test_changed_activity_fails_without_modifying_gn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            java, activity, gn = self.make_tree(root)
            activity.write_text('unexpected upstream contents')
            before = gn.read_bytes()
            result = subprocess.run(['python3', str(INSTALL), str(root)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(gn.read_bytes(), before)
            self.assertFalse((java / 'PsychoTabController.java').exists())


if __name__ == '__main__':
    unittest.main()
