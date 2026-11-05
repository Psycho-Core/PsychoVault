# My build and release rules

## 1. Toolchain

| Piece | What I use | Notes |
| --- | --- | --- |
| Compiler | MSVC (Windows x64, C++20) | `cl.exe` from the Windows SDK toolchain |
| Local build (engine/tests) | `tools/Psycho_build.py` | Builds and runs the engine test suite on the development machine |
| Windows build | `tools/Psycho_build.py` (Windows target) | Produces `PsychoVault.exe`, `PsychoVaultRenderer.exe`, `PsychoVaultProxy.exe` |
| Installer | `installer/` (built with the same toolchain) | Produces `PsychoVault-Setup-<version>.exe` |
| Packaging | `tools/Psycho_package.py` | Builds the installer payload manifest with per-file hashes |
| Test images | my own golden-image and diff tooling in `tests/` | No third-party diff service |

No third-party runtime library ships inside the product. Build-time tools never ship.

## 2. Local build order

1. `python tools/Psycho_build.py --target engine` - engine and unit tests, runs on the development machine.
2. `python tools/Psycho_build.py --target tests` - full unit + golden-image suite.
3. `python tools/Psycho_build.py --target windows` - broker, renderer, proxy daemon.
4. `python tools/Psycho_package.py --stage` - payload staging with hashes.
5. `installer` build - `PsychoVault-Setup-<version>.exe`.
6. Install it on a clean profile, run the milestone's acceptance script, record the evidence.

## 3. GitHub Actions policy

- **Until M2: workflows stay parked at `workflow_dispatch` only.** No push, tag or pull-request trigger may start a build. A build is only worth running when its gate is real.
- When CI returns (S240), it runs: engine unit tests, golden images, runtime integration tests, and a manual-dispatch Windows installer job. Nothing else.
- One heavy Windows job at a time (shared concurrency group), no parallel duplicate builds, no tag-triggered release until M9.
- A green build is not evidence that a feature works; it is evidence that the code compiles and the tests that exist passed.

## 4. Release rules

- The release artefact is the **installer**, plus its SHA-256 and the signed update manifest. Never a bare portable exe.
- A release requires: the milestone gate met, the installer tested on a clean machine, notes in my voice listing what works, what does not and what was not run.
- No release notes claim behaviour that has no test attached.
- Version numbers come from `Psycho_VERSION.txt`; tags are `v<version>` and are only created for a release that passed its gate.

## 5. Evidence format

Every completed step records, in the milestone report:

```
step: S055 (flexbox)
test: tests/Psycho_layout_flex_test.cpp - 41 cases
result: passed 41 / failed 0 / skipped 0
artefacts: build/evidence/S055/  (golden images + diff report)
notes: gap - flex-basis auto with min-content not implemented, tracked as S055b
```

Blocked and not-run results are written out in full. A step without a test line is not done.
