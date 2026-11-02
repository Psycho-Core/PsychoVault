from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'engine' / 'psychovault' / 'configure_build_args.py'
UPSTREAM = ROOT / 'engine' / 'titanium' / 'args.gn'
sys.path.insert(0, str(SCRIPT.parent))
import configure_build_args as cba  # noqa: E402


class BuildArgsTests(unittest.TestCase):
    def test_hosted_args_keep_release_limits_and_arm64(self):
        args = cba.build_args(UPSTREAM.read_text(), '')
        self.assertIn('target_cpu = "arm64"', args)
        self.assertIn('symbol_level = 0', args)
        self.assertIn('use_siso = false', args)
        self.assertIn('generate_linker_map = false', args)
        self.assertIn('chrome_pgo_phase = 0', args)
        self.assertIn('use_thin_lto = false', args)
        self.assertIn('android_static_analysis = "off"', args)
        self.assertNotIn('cc_wrapper', args)
        self.assertNotIn('target_cpu = "arm"', args)

    def test_sccache_wrapper_is_explicit_and_optional(self):
        args = cba.build_args(UPSTREAM.read_text(), 'sccache')
        self.assertIn('cc_wrapper = "sccache"', args)
        self.assertIn('chrome_pgo_phase = 0', args)

    def test_unknown_wrapper_is_refused(self):
        with self.assertRaises(ValueError):
            cba.build_args(UPSTREAM.read_text(), 'not-a-wrapper')

    def test_changed_upstream_anchor_is_refused(self):
        with self.assertRaises(ValueError):
            cba.build_args('target_cpu = "x64"\n', '')

    def test_cli_writes_output_and_rejects_bad_wrapper(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'out' / 'args.gn'
            subprocess.run([sys.executable, str(SCRIPT), '--upstream', str(UPSTREAM), '--output', str(output), '--wrapper', 'sccache'], check=True)
            self.assertIn('cc_wrapper = "sccache"', output.read_text())
            bad = subprocess.run([sys.executable, str(SCRIPT), '--upstream', str(UPSTREAM), '--output', str(output), '--wrapper', 'bad'], capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('Unsupported compiler wrapper', bad.stderr)


if __name__ == '__main__':
    unittest.main()
