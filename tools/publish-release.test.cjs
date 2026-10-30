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

test('--check refuses to run without a token, like publishing does', () => {
  const r = run(['--check'])
  assert.notStrictEqual(r.status, 0)
  assert.match(r.stderr, /no token/)
})

test('--check never prints the token, and reports a network failure as a sentence', () => {
  // These consoles get pasted into chat, so the value must not appear even in part.
  const secret = 'github_pat_' + 'x'.repeat(40)
  const r = run(['--check'], { GITHUB_TOKEN: secret })
  const output = (r.stdout || '') + (r.stderr || '')
  assert.ok(!output.includes(secret), 'the token was printed')
  assert.ok(!output.includes('xxxxxxxxxxxxxxxx'), 'part of the token was printed')
  assert.match(output, /Token source: GITHUB_TOKEN/)
  // Either GitHub answered and refused it, or it could not be reached. Both must read as
  // sentences, never as an unhandled rejection with a stack trace.
  assert.ok(/GitHub (could not be reached|answered|was not found)|token (was refused|is not allowed)/.test(output),
    'the failure was not explained: ' + output)
  assert.ok(!/at async|node:internal|TypeError/.test(output), 'a stack trace leaked into the output')
})

// The ways a hand-made token file goes wrong, kept as tests because each one used to look
// exactly like "no token at all".
const tokenFile = path.join(root, 'publish-token.json')
const txtFile = path.join(root, 'publish-token.json.txt')

function withFile(file, bytes, fn) {
  fs.writeFileSync(file, bytes)
  try { return fn() } finally { fs.rmSync(file, { force: true }) }
}

test('a token file saved by Notepad as UTF-16 is still read', () => {
  const token = 'github_pat_' + 'a'.repeat(40)
  const utf16 = Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(JSON.stringify({ token }), 'utf16le')])
  withFile(tokenFile, utf16, () => {
    const r = run(['--check'])
    assert.match(r.stdout, /Token source: publish-token\.json/, 'a UTF-16 token file was not read')
    assert.ok(!(r.stdout + r.stderr).includes(token), 'the token was printed')
  })
})

test('a bare token on its own line is accepted', () => {
  const token = 'github_pat_' + 'b'.repeat(40)
  withFile(tokenFile, token + '\r\n', () => {
    const r = run(['--check'])
    assert.match(r.stdout, /Token source: publish-token\.json/)
  })
})

test('a file called publish-token.json.txt is named as the problem', () => {
  const token = 'github_pat_' + 'c'.repeat(40)
  withFile(txtFile, JSON.stringify({ token }), () => {
    const r = run(['--check'])
    assert.notStrictEqual(r.status, 0)
    assert.match(r.stderr, /rename it to publish-token\.json/)
  })
})

test('an empty or malformed token file says which it is', () => {
  withFile(tokenFile, '', () => {
    assert.match(run(['--check']).stderr, /exists but the file is empty/)
  })
  withFile(tokenFile, 'not json at all', () => {
    assert.match(run(['--check']).stderr, /exists but it is not valid JSON/)
  })
  withFile(tokenFile, '{"tokeny":"x"}', () => {
    assert.match(run(['--check']).stderr, /no "token" field/)
  })
})

test('typographic quotes and invisible characters are normalised, not rejected', () => {
  // Pasting from a web page gives “ ” instead of " ", and a zero-width space often comes along
  // with it. Both used to read as "no token".
  const token = 'github_pat_' + 'd'.repeat(40)
  withFile(tokenFile, Buffer.from('{\u201ctoken\u201d:\u201c' + token + '\u201d}\n', 'utf8'), () => {
    assert.match(run(['--check']).stdout, /Token source: publish-token\.json/, 'curly quotes were rejected')
  })
  withFile(tokenFile, Buffer.from('{"token":"' + token.slice(0, 12) + '\u200b' + token.slice(12) + '"}\n', 'utf8'), () => {
    assert.match(run(['--check']).stdout, /Token source: publish-token\.json/, 'a zero-width space was rejected')
  })
})
