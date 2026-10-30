'use strict'
// Guards on tools/publish-release.mjs - the step that turns a finished build into a GitHub
// release. The network calls are not exercised here; what is checked is that it refuses the
// cases it must refuse, and that the release it would post carries the notes and the exact
// asset names the in-app updater looks for.
const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')
const { spawnSync } = require('node:child_process')

const root = path.join(__dirname, '..')
const script = path.join(root, 'tools', 'publish-release.mjs')
const version = require(path.join(root, 'app', 'package.json')).version
const installer = path.join(root, `PsychoVault-Setup-${version}.exe`)
const notes = path.join(root, 'release-notes', `${version}.txt`)

function run(args, env = {}) {
  const clean = Object.assign({}, process.env)
  delete clean.GITHUB_TOKEN
  delete clean.GH_TOKEN
  return spawnSync(process.execPath, [script, ...args], { cwd: root, env: Object.assign(clean, env), encoding: 'utf8' })
}

test('refuses to publish without a token, and says where the token goes', () => {
  const had = fs.existsSync(installer)
  if (!had) fs.writeFileSync(installer, 'stub')
  try {
    const r = run(['--dry-run'])
    assert.notStrictEqual(r.status, 0, 'it exited successfully with no token')
    assert.match(r.stderr, /no token/)
    assert.match(r.stderr, /publish-token\.json/)
  } finally {
    if (!had) fs.rmSync(installer, { force: true })
  }
})

test('refuses to publish something that was never built', () => {
  const r = run(['--dry-run'], { GITHUB_TOKEN: 'dummy' })
  assert.notStrictEqual(r.status, 0)
  assert.match(r.stderr, /no installer to publish/)
})

test('the release body carries the notes file, the asset name and the real hash', () => {
  const bytes = crypto.randomBytes(4096)
  fs.writeFileSync(installer, bytes)
  try {
    const r = run(['--dry-run'], { GITHUB_TOKEN: 'dummy' })
    assert.strictEqual(r.status, 0, r.stderr)
    const body = r.stdout
    const expectedHash = crypto.createHash('sha256').update(bytes).digest('hex')
    assert.ok(body.includes(expectedHash), 'the release body does not carry the installer hash')
    assert.ok(body.includes(`PsychoVault-Setup-${version}.exe`), 'the asset name is missing from the body')
    const firstNote = fs.readFileSync(notes, 'utf8').split('\n')[0].trim()
    if (firstNote) assert.ok(body.includes(firstNote), 'the release body does not include release-notes/' + version + '.txt')
    // signed or not, the body has to say which, because the updater ignores one of the two
    assert.match(body, /Update manifest: (\*\*not signed\*\*|signed)/)
  } finally {
    fs.rmSync(installer, { force: true })
  }
})

test('the build asks before publishing and never fails a good build over it', () => {
  const bat = fs.readFileSync(path.join(root, 'PsychoVault_easy_build.bat'), 'utf8')
  assert.match(bat, /\[9\/9\] Publishing the release to GitHub/)
  assert.match(bat, /Post this build as release v%VERSION% on GitHub\? \[y\/N\]/)
  assert.match(bat, /node "%ROOT%\\tools\\publish-release\.mjs"/)
  assert.match(bat, /if defined PV_PUBLISH goto :publish_decided/, 'there is no way to publish without being asked')
  // both failure routes end at :done, not at a failure label
  const failed = bat.slice(bat.indexOf(':publish_failed'), bat.indexOf('\n:done', bat.indexOf(':publish_failed')))
  assert.match(failed, /goto :done/, 'a failed publish does not return to the normal end of the build')
})

test('a signing key kept out of Git lets a local build sign the manifest', () => {
  const sign = fs.readFileSync(path.join(root, 'tools', 'sign-update-manifest.mjs'), 'utf8')
  assert.match(sign, /function localKeyFile\(\)/)
  assert.match(sign, /process\.env\.UPDATE_SIGNING_PRIVATE_KEY_B64 \|\| localKeyFile\(\)/)
})
