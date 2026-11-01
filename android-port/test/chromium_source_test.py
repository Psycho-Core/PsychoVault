from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'engine' / 'psychovault'
INSTALL = ROOT / 'install_chromium_source.py'


class ChromiumSourceTests(unittest.TestCase):
    def make_tree(self, path):
        java = path / 'chrome/android/java/src/org/chromium/chrome/browser'
        java.mkdir(parents=True)
        activity = java / 'ChromeTabbedActivity.java'
        activity.write_text('public class ChromeTabbedActivity extends ChromeActivity implements PreAttachIntentObserver {\n}\n')
        gn = path / 'chrome/android/chrome_java_sources.gni'
        gn.write_text('chrome_java_sources = [\n  "java/src/org/chromium/chrome/browser/ChromeTabbedActivity.java",\n]\n')
        return java, activity, gn

    def test_controller_is_added_to_activity_and_gn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            java, activity, gn = self.make_tree(root)
            subprocess.run(['python3', str(INSTALL), str(root)], check=True)
            self.assertIn('createPsychoTabController()', activity.read_text())
            self.assertIn('PsychoTabController.java', gn.read_text())
            self.assertEqual((java / 'PsychoTabController.java').read_bytes(), (ROOT / 'java/org/chromium/chrome/browser/PsychoTabController.java').read_bytes())
            old = activity.read_bytes()
            result = subprocess.run(['python3', str(INSTALL), str(root)], capture_output=True)
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
