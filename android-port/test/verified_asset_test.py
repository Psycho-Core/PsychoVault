import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

FILE = Path(__file__).resolve().parents[1] / 'engine' / 'psychovault' / 'prepare_bundled_extension.py'
spec = importlib.util.spec_from_file_location('pinned_extension', FILE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VerifiedAssetTests(unittest.TestCase):
    def test_verified_content_replaces_destination_atomically(self):
        body = b'synthetic extension'
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'cache' / 'titanium.crx'
            with patch.object(module.urllib.request, 'urlopen', return_value=io.BytesIO(body)):
                module.download_verified('https://github.com/example/file', hashlib.sha256(body).hexdigest(), 1024, target)
            self.assertEqual(target.read_bytes(), body)
            self.assertEqual(list(target.parent.iterdir()), [target])

    def test_bad_hash_and_oversize_leave_previous_file_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'file.crx'
            target.write_bytes(b'prior')
            for digest, limit in [('0' * 64, 100), (hashlib.sha256(b'bad').hexdigest(), 1)]:
                with patch.object(module.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad')):
                    with self.assertRaises(ValueError):
                        module.download_verified('https://github.com/example/file', digest, limit, target)
                self.assertEqual(target.read_bytes(), b'prior')
                self.assertEqual(list(Path(tmp).iterdir()), [target])

    def test_invalid_initial_origin_and_digest_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'file.crx'
            with self.assertRaises(ValueError):
                module.download_verified('https://evil.example/file', '0' * 64, 100, target)
            with self.assertRaises(ValueError):
                module.download_verified('https://github.com/example/file', 'x' * 64, 100, target)
            self.assertFalse(target.exists())


if __name__ == '__main__':
    unittest.main()
