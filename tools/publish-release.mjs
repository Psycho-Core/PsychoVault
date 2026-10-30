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

const tag = `v${version}`
const installerName = `${PRODUCT}-Setup-${version}.exe`
const installerPath = path.join(ROOT, installerName)
const notesPath = path.join(ROOT, 'release-notes', `${version}.txt`)
const generatedNotesPath = path.join(ROOT, `Release-Notes-${version}.txt`)

function fail(message) {
  console.error('publish-release: ' + message)
  process.exit(1)
}

function token() {
  for (const name of ['GITHUB_TOKEN', 'GH_TOKEN']) {
    const value = String(process.env[name] || '').trim()
    if (value) return { value, source: name }
  }
  // publish-token.json lives at the ROOT, not in app/: everything in app/ is copied into the
  // payload and shipped to users, and a token with write access must never travel with the
  // browser. app/update-token.json is the read-only token the updater uses.
  for (const file of [path.join(ROOT, 'publish-token.json'), path.join(ROOT, 'app', 'update-token.json')]) {
    try {
      const parsed = JSON.parse(fs.readFileSync(file, 'utf8'))
      const value = String((parsed && parsed.token) || '').trim()
      if (value) return { value, source: path.relative(ROOT, file) }
    } catch { /* not there, try the next one */ }
  }
  return { value: '', source: '' }
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
  const res = await fetch(url, { method, headers, body })
  const text = await res.text()
  let json = null
  try { json = text ? JSON.parse(text) : null } catch { /* not JSON */ }
  return { status: res.status, ok: res.ok, json, text }
}

function explain(status, text) {
  if (status === 401) return 'the token was refused (401). It may be expired, revoked, or not a token at all.'
  if (status === 403) return 'the token is not allowed to do this (403). Publishing needs contents:write on ' + REPOSITORY + '.'
  if (status === 404) return 'the repository was not found (404). For a private repository the token must have access to it.'
  if (status === 422) return 'GitHub rejected the request (422): ' + text
  return 'GitHub answered ' + status + ': ' + text
}

if (!fs.existsSync(installerPath)) fail('no installer to publish: ' + installerPath + ' does not exist. Build it first.')

const auth = token()
if (!auth.value) {
  fail('no token. Set GITHUB_TOKEN, or put {"token":"..."} in publish-token.json at the repo root (contents:write on ' + REPOSITORY + ').')
}

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
