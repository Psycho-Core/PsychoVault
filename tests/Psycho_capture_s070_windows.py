import base64
import pathlib
import sys
import zlib
sys.path.insert(0, 'tools')
import Psycho_golden as g
out = pathlib.Path('build/s070-candidate/candidates')
for name in g.CASES:
    candidate = out / (name + '.png')
    specific = g.BASELINES / 'windows' / (name + '.png')
    baseline = specific if specific.exists() else g.BASELINES / 'common' / (name + '.png')
    if not baseline.exists() or g.decode_png(candidate) != g.decode_png(baseline):
        encoded = base64.b64encode(zlib.compress(candidate.read_bytes(), 9)).decode()
        print(f'::warning title=S070Baseline-{name}::{encoded}')
