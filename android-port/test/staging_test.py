from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StagingTests(unittest.TestCase):
    def test_hook_is_pinned_and_fails_on_unrecognised_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            shutil.copy(ROOT / 'engine' / 'titanium' / '.gclient', stage / '.gclient')
            shutil.copy(ROOT / 'engine' / 'titanium' / 'args.gn', stage / 'args.gn')
            shutil.copy(ROOT / 'engine' / 'titanium' / 'patch.sh', stage / 'patch.sh')
            shutil.copytree(ROOT / 'engine' / 'psychovault', stage / 'psychovault', ignore=shutil.ignore_patterns('__pycache__'))
            configure = stage / 'psychovault' / 'configure_stage.py'
            subprocess.run(['python3', str(configure)], check=True)
            text = (stage / '.gclient').read_text()
            self.assertIn('../psychovault/prepare_bundled_extension.py', text)
            self.assertNotIn('releases/latest/download/titanium.crx', text)
            self.assertIn('fetch_filter_lists', text)
            self.assertIn('install_chromium_source.py', (stage / 'patch.sh').read_text())
            args = (stage / 'args.gn').read_text()
            self.assertIn('chrome_public_manifest_package = "com.psychocore.psychovault"', args)
            self.assertIn('target_cpu = "arm"', args)
            self.assertNotIn('io.github.jqssun.helium', args)
            second = subprocess.run(['python3', str(configure)], capture_output=True, text=True)
            self.assertNotEqual(second.returncode, 0)
            self.assertIn('hook changed', second.stderr)
            self.assertEqual((stage / '.gclient').read_text(), text)


if __name__ == '__main__':
    unittest.main()
