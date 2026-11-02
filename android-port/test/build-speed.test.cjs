const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const workflow = fs.readFileSync(path.join(root, '../.github/workflows/android-build.yml'), 'utf8');
const segment = fs.readFileSync(path.join(root, '../.github/workflows/android-build-segment.yml'), 'utf8');
const build = fs.readFileSync(path.join(root, 'scripts/build-engine.sh'), 'utf8');
const prepare = fs.readFileSync(path.join(root, 'scripts/prepare-engine.sh'), 'utf8');

test('hosted APK build uses a pinned sccache action and GitHub-backed cache', () => {
  assert.match(segment, /mozilla-actions\/sccache-action@[a-f0-9]{40}/);
  assert.match(segment, /version: v0\.16\.0/);
  assert.match(segment, /PSYCHO_CC_WRAPPER: sccache/);
  assert.match(segment, /SCCACHE_GHA_ENABLED: "true"/);
  assert.match(segment, /SCCACHE_GHA_VERSION: android-arm64-v1/);
  assert.match(segment, /SCCACHE_BASEDIRS: \$\{\{ steps\.paths\.outputs\.engine_root \}\}\/chromium\/src/);
});

test('hosted APK build keeps a stable engine path and refuses to overwrite it', () => {
  assert.match(segment, /engine="\$parent\/psychovault-engine"/);
  assert.doesNotMatch(segment, /psychovault-engine-\$\{GITHUB_RUN_ID\}/);
  assert.match(segment, /Engine workspace already exists; refusing to overwrite/);
  assert.match(prepare, /not overwriting/);
});

test('APK build uses runner cores instead of a hard-coded four jobs', () => {
  assert.doesNotMatch(build, /autoninja -C out\/Default -j 4 /);
  assert.match(build, /jobs="\$\{PSYCHO_BUILD_JOBS:-\$\(nproc\)\}"/);
  assert.match(build, /autoninja -C out\/Default -j "\$jobs" chrome_public_apk/);
  assert.match(build, /PSYCHO_BUILD_JOBS must be a positive integer/);
});

test('APK build records compiler-cache stats and keeps release limits', () => {
  assert.match(build, /sccache --zero-stats/);
  assert.match(build, /sccache --show-stats \| tee "\$NOTES\/sccache\.txt"/);
  assert.match(build, /configure_build_args\.py/);
  const args = fs.readFileSync(path.join(root, 'engine/psychovault/configure_build_args.py'), 'utf8');
  assert.match(args, /chrome_pgo_phase = 0/);
  assert.match(args, /use_thin_lto = false/);
  assert.match(args, /symbol_level = 0/);
  assert.match(args, /android_static_analysis = "off"/);
});

test('running APK builds are protected unless a restart is explicitly authorized', () => {
  assert.match(workflow, /group: psychovault-android-build/);
  assert.match(workflow, /cancel-in-progress: \$\{\{ github\.event_name == 'push' && contains\(github\.event\.head_commit\.message \|\| '', '\[restart build\]'\) \}\}/);
  assert.match(segment, /runs-on: \$\{\{ vars\.ANDROID_RUNNER \|\| 'ubuntu-24\.04' \}\}/);
  assert.match(segment, /PSYCHO_BUILD_JOBS: \$\{\{ vars\.ANDROID_BUILD_JOBS \}\}/);
  assert.match(workflow, /branches: \[arena\/882adae6-psychovault\]/);
});

test('long hosted compiles checkpoint before the six-hour job limit and resume Ninja', () => {
  assert.match(segment, /timeout-minutes: 360/);
  assert.match(segment, /PSYCHO_MAX_BUILD_MINUTES: '240'/);
  assert.match(build, /timeout --signal=INT --kill-after=120s "\$\{MAX_BUILD_MINUTES\}m"/);
  assert.match(build, /printf 'checkpoint\\n' > "\$STATUS_FILE"/);
  assert.match(build, /PSYCHO_RESUME_ENGINE/);
  assert.match(build, /\.psychovault-engine-ready/);
  assert.match(segment, /android-engine-checkpoint-/);
  assert.match(segment, /sha256sum -c SHA256SUMS/);
  assert.match(segment, /compression-level: 0/);
  assert.match(workflow, /resume_artifact: \$\{\{ needs\.segment-1\.outputs\.checkpoint_artifact \}\}/);
  assert.match(workflow, /needs\.segment-1\.outputs\.status == 'checkpoint'/);
});
