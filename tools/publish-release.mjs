#!/usr/bin/env node
// Publish a finished local build as a GitHub release, with the notes the build already wrote.
//
// The in-app updater reads /releases/latest, then fetches two assets by name:
//   update-manifest.json, update-manifest.sig  - signed metadata (verifyUpdateManifest)
//   PsychoVault-Setup-<version>.exe            - the installer itself
// so a release published without those is invisible to the updater even though it looks fine on
// GitHub. This script uploads what exists, names what it skipped and why.
//
// Auth, in order: GITHUB_TOKEN / GH_TOKEN, publish-token.json at the repo root (see
// publish-token.example.json), then the read-only app/update-token.json.
// A read token is enough to check; publishing needs contents:write on this repository.
//
// Usage: node tools/publish-release.mjs [--dry-run] [--draft]
import fs from 'node:fs'
import path from 'node:path'
import crypto from 'node:crypto'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)
const ROOT = path.resolve(import.meta.dirname, '..')
const { REPOSITORY, PRODUCT } = require('../app/update-manifest.js')
const version = require('../app/package.json').version
const args = process.argv.slice(2)
const dryRun = args.includes('--dry-run')
const draft = args.includes('--draft')
const check = args.includes('--check')

const tag = `v${version}`
const installerName = `${PRODUCT}-Setup-${version}.exe`
const installerPath = path.join(ROOT, installerName)
const notesPath = path.join(ROOT, 'release-notes', `${version}.txt`)
const generatedNotesPath = path.join(ROOT, `Release-Notes-${version}.txt`)

function fail(message) {
  console.error('publish-release: ' + message)
  process.exit(1)
}

// Notepad is the obvious way to make this file, and Notepad's default "Save as type" is Text
// Documents - which quietly produces publish-token.json.txt - and its Encoding menu offers UTF-16.
// A UTF-16 file read as utf8 is full of NUL bytes and JSON.parse throws, so the token looked
// exactly like no token at all. Both are handled here, and anything that still fails is reported
// with the reason instead of being skipped.
function readTokenFile(file) {
  let raw
  try { raw = fs.readFileSync(file) }
  catch (e) { return { missing: true } }
  let text
  if (raw.length >= 2 && raw[0] === 0xff && raw[1] === 0xfe) text = raw.toString('utf16le')
  else if (raw.length >= 2 && raw[1] === 0x00 && raw[0] !== 0x00) text = raw.toString('utf16le')
  else text = raw.toString('utf8')
  // A token pasted from a web page or typed on a keyboard that produces typographic quotes
  // arrives as \u201C/\u201D, which is not JSON and used to read as "no token at all". Invisible
  // characters come along for the ride too. Normalise before parsing, and only then complain.
  text = text
    .replace(/[\u2018\u2019\u201A\u201B]/g, "'")
    .replace(/[\u201C\u201D\u201E\u201F]/g, '"')
    .replace(/[\u200B-\u200D\uFEFF]/g, '')
    .replace(/\u00A0/g, ' ')
    .trim()
  if (!text) return { missing: false, problem: 'the file is empty' }
  // A bare token on its own line is accepted too: it is what people paste.
  if (/^(github_pat_|ghp_|gho_|ghu_|ghs_)/.test(text) && !text.startsWith('{')) return { value: text }
  try {
    const parsed = JSON.parse(text)
    const value = String((parsed && parsed.token) || '').trim()
    if (value) return { value }
    return { missing: false, problem: 'it is JSON but has no "token" field' }
  } catch (e) {
    return { missing: false, problem: 'it is not valid JSON (' + e.message + ')' }
  }
}

function token() {
  const notes = []
  for (const name of ['GITHUB_TOKEN', 'GH_TOKEN']) {
    const value = String(process.env[name] || '').trim()
    if (value) return { value, source: name, notes }
  }
  // publish-token.json lives at the ROOT, not in app/: everything in app/ is copied into the
  // payload and shipped to users, and a token with write access must never travel with the
  // browser. app/update-token.json is the read-only token the updater uses.
  for (const file of [path.join(ROOT, 'publish-token.json'), path.join(ROOT, 'app', 'update-token.json')]) {
    const result = readTokenFile(file)
    const shown = path.relative(ROOT, file)
    if (result.value) return { value: result.value, source: shown, notes }
    if (result.problem) notes.push(shown + ' exists but ' + result.problem)
  }
  // The two ways this file goes wrong invisibly: a .txt extension from Notepad, and the right
  // name in the wrong place.
  try {
    for (const name of fs.readdirSync(ROOT)) {
      if (name === 'publish-token.json') continue
      if (/^publish.*token.*(\.json\.txt|\.json|)$/i.test(name) && fs.statSync(path.join(ROOT, name)).isFile()) {
        const result = readTokenFile(path.join(ROOT, name))
        if (result.value) notes.push('found a token in "' + name + '" - rename it to publish-token.json')
        else notes.push('"' + name + '" looks like it was meant to be publish-token.json' + (result.problem ? ' (' + result.problem + ')' : ''))
      }
    }
  } catch { /* unreadable folder is not worth failing over */ }
  return { value: '', source: '', notes }
}

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')
}

// A release body that says what changed, then what was actually built. Nothing invented: every
// line comes from a file on disk or from the installer itself.
function buildBody() {
  const parts = []
  let notes = ''
  for (const candidate of [notesPath, generatedNotesPath]) {
    try { notes = fs.readFileSync(candidate, 'utf8').trim() } catch { continue }
    if (notes) break
  }
  if (notes) parts.push(notes)
  else parts.push('No notes file was found for ' + version + ', so this release has none.')

  const lines = ['', '---', '', `**Built by** \`PsychoVault_easy_build.bat\` on ${new Date().toISOString().slice(0, 10)}`, '']
  lines.push(`- Installer: \`${installerName}\``)
  lines.push(`- Size: ${fs.statSync(installerPath).size.toLocaleString('en-US')} bytes`)
  lines.push(`- SHA-256: \`${sha256(installerPath)}\``)
  const manifestPath = path.join(ROOT, 'update-manifest.json')
  if (fs.existsSync(manifestPath) && fs.existsSync(path.join(ROOT, 'update-manifest.sig'))) {
    lines.push('- Update manifest: signed, so the in-app updater can verify this release')
  } else {
    lines.push('- Update manifest: **not signed**, so the in-app updater will ignore this release')
  }
  lines.push('')
  lines.push('The build is unsigned, so SmartScreen will warn on first run: choose More info, then Run anyway.')
  parts.push(lines.join('\n'))
  return parts.join('\n\n')
}

async function api(method, url, { body, accept = 'application/vnd.github+json', contentType = 'application/json' } = {}) {
  const headers = {
    authorization: 'Bearer ' + auth.value,
    accept,
    'user-agent': 'PsychoVault-Publish/1.0',
    'x-github-api-version': '2022-11-28'
  }
  if (body) headers['content-type'] = contentType
  let res
  try {
    res = await fetch(url, { method, headers, body })
  } catch (e) {
    // No network, a proxy, or a TLS interception. A stack trace here reads like the script
    // crashed; it is only "GitHub could not be reached", which the user can act on.
    return { status: 0, ok: false, json: null, text: String((e && e.cause && e.cause.message) || (e && e.message) || e) }
  }
  const text = await res.text()
  let json = null
  try { json = text ? JSON.parse(text) : null } catch { /* not JSON */ }
  return { status: res.status, ok: res.ok, json, text }
}

function explain(status, text) {
  if (status === 0) return 'GitHub could not be reached: ' + text + '. Check the connection, or any proxy or antivirus that inspects HTTPS.'
  if (status === 401) return 'the token was refused (401). It may be expired, revoked, or not a token at all.'
  if (status === 403) return 'the token is not allowed to do this (403). Publishing needs contents:write on ' + REPOSITORY + '.'
  if (status === 404) return 'the repository was not found (404). For a private repository the token must have access to it.'
  if (status === 422) return 'GitHub rejected the request (422): ' + text
  return 'GitHub answered ' + status + ': ' + text
}

const auth = token()

// --check answers "will publishing work?" without publishing anything: which token was found,
// whether it can see the private repository at all, and whether it is allowed to write.
// A token that can only read gets exactly as far as one that does not exist at publish time,
// and finding that out after a 20-minute build is the wrong time to find it out.
if (check) {
  if (!auth.value) {
    const detail = auth.notes.length ? '\n  ' + auth.notes.join('\n  ') + '\n' : ''
    fail('no token. Put {"token":"..."} in publish-token.json at the repo root, or set GITHUB_TOKEN.' + detail)
  }
  // The value itself is never printed, not even part of it: these consoles get pasted into chats.
  const shape = auth.value.startsWith('github_pat_') ? 'fine-grained token' : auth.value.startsWith('ghp_') ? 'classic token' : 'unrecognised token shape'
  console.log('Token source: ' + auth.source + ' (' + shape + ', ' + auth.value.length + ' characters)')
  const repo = await api('GET', `https://api.github.com/repos/${REPOSITORY}`)
  if (!repo.ok) {
    fail(explain(repo.status, repo.text))
  }
  const canPush = !!(repo.json && repo.json.permissions && repo.json.permissions.push)
  console.log('Repository:  ' + REPOSITORY + (repo.json && repo.json.private ? ' (private)' : ' (public)'))
  console.log('Read access: yes')
  console.log('Write access: ' + (canPush ? 'yes - publishing will work' : 'NO - this token needs Contents: Read and write'))
  const releases = await api('GET', `https://api.github.com/repos/${REPOSITORY}/releases?per_page=1`)
  console.log('Releases:    ' + (releases.ok ? 'readable' : 'not readable (' + releases.status + ')'))
  if (!canPush) {
    console.log('\nEdit the token at https://github.com/settings/tokens?type=beta and add')
    console.log('Repository permissions -> Contents: Read and write, then save.')
  }
  process.exit(canPush ? 0 : 1)
}

if (!auth.value) {
  const detail = auth.notes.length ? '\n  ' + auth.notes.join('\n  ') + '\n' : ''
  fail('no token. Put {"token":"..."} in publish-token.json at the repo root (contents:write on ' + REPOSITORY + '), or set GITHUB_TOKEN.' + detail)
}
if (!fs.existsSync(installerPath)) fail('no installer to publish: ' + installerPath + ' does not exist. Build it first.')

const assets = [{ file: installerPath, name: installerName }]
const manifestPath = path.join(ROOT, 'update-manifest.json')
const sigPath = path.join(ROOT, 'update-manifest.sig')
const signed = fs.existsSync(manifestPath) && fs.existsSync(sigPath)
if (signed) {
  assets.push({ file: manifestPath, name: 'update-manifest.json' })
  assets.push({ file: sigPath, name: 'update-manifest.sig' })
}

console.log(`Publishing ${tag} from ${auth.source}` + (dryRun ? ' (dry run)' : ''))
console.log(`  installer: ${installerName} (${fs.statSync(installerPath).size} bytes)`)
console.log('  update manifest: ' + (signed ? 'signed, will be uploaded' : 'not signed, so the in-app updater will skip this release'))

if (dryRun) {
  const body = buildBody()
  console.log('\nRelease body that would be posted:\n---\n' + body + '\n---')
  process.exit(0)
}

const base = `https://api.github.com/repos/${REPOSITORY}`
let release = null

const existing = await api('GET', `${base}/releases/tags/${encodeURIComponent(tag)}`)
if (existing.ok && existing.json) {
  release = existing.json
  console.log(`  ${tag} already exists - updating it rather than creating a second one`)
} else if (existing.status !== 404) {
  fail(explain(existing.status, existing.text))
}

const body = buildBody()

if (release) {
  const patched = await api('PATCH', `${base}/releases/${release.id}`, {
    body: JSON.stringify({ name: `${PRODUCT} ${version}`, body, draft, prerelease: false })
  })
  if (!patched.ok) fail(explain(patched.status, patched.text))
  release = patched.json
} else {
  const created = await api('POST', `${base}/releases`, {
    body: JSON.stringify({
      tag_name: tag,
      target_commitish: 'Master',
      name: `${PRODUCT} ${version}`,
      body,
      draft,
      prerelease: false
    })
  })
  if (!created.ok) fail(explain(created.status, created.text))
  release = created.json
  console.log(`  created ${tag}`)
}

// Re-uploading means replacing: GitHub rejects a second asset with the same name, and a stale
// installer left under the same name would be worse than an error.
const listed = await api('GET', `${base}/releases/${release.id}/assets?per_page=100`)
const onGitHub = listed.ok && Array.isArray(listed.json) ? listed.json : []
for (const asset of assets) {
  const clash = onGitHub.find((a) => a.name === asset.name)
  if (clash) {
    const removed = await api('DELETE', `${base}/releases/assets/${clash.id}`)
    if (!removed.ok) fail(explain(removed.status, 'could not replace the existing ' + asset.name + ': ' + removed.text))
    console.log('  replaced ' + asset.name)
  }
  const uploadUrl = `https://uploads.github.com/repos/${REPOSITORY}/releases/${release.id}/assets?name=${encodeURIComponent(asset.name)}`
  const uploaded = await api('POST', uploadUrl, {
    body: fs.readFileSync(asset.file),
    contentType: 'application/octet-stream'
  })
  if (!uploaded.ok) fail(explain(uploaded.status, 'uploading ' + asset.name + ': ' + uploaded.text))
  console.log('  uploaded ' + asset.name + ' (' + uploaded.json.size.toLocaleString('en-US') + ' bytes)')
}

console.log('\nRelease: ' + (release.html_url || `${base}/releases/tag/${tag}`))
if (!signed) {
  console.log('Note: this release has no signed update manifest, so the in-app update banner will')
  console.log('      not offer it. Run tools/sign-update-manifest.mjs before publishing to change that.')
}
