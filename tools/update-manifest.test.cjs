'use strict'

const test = require('node:test')
const assert = require('node:assert/strict')
const crypto = require('node:crypto')
const {
  keyIdForPublicKey,
  compareVersions,
  verifyUpdateManifest,
  installerDownloadUrl,
  classifyCheckFailure
} = require('../app/update-manifest.js')

test('verifies an Ed25519-signed installer manifest for a newer Windows release', () => {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519')
  const publicPem = publicKey.export({ type: 'spki', format: 'pem' })
  const manifest = {
    schemaVersion: 1,
    product: 'PsychoVault',
    repository: 'Psycho-Core/PsychoVault',
    platform: 'win32-x64',
    version: '1.0.4',
    tag: 'v1.0.4',
    assetName: 'PsychoVault-Setup-1.0.4.exe',
    sha256: 'a'.repeat(64),
    size: 123456,
    keyId: keyIdForPublicKey(publicPem)
  }
  const bytes = Buffer.from(JSON.stringify(manifest, null, 2) + '\n')
  const signature = crypto.sign(null, bytes, privateKey).toString('base64')
  const result = verifyUpdateManifest(bytes, signature, publicPem, {
    currentVersion: '1.0.3',
    tag: 'v1.0.4'
  })
  assert.equal(result.ok, true)
  assert.equal(result.manifest.version, '1.0.4')
  assert.equal(installerDownloadUrl(result.manifest), 'https://github.com/Psycho-Core/PsychoVault/releases/download/v1.0.4/PsychoVault-Setup-1.0.4.exe')
})

test('rejects changed bytes, invalid signatures, wrong release tags, and unsupported platforms', () => {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519')
  const publicPem = publicKey.export({ type: 'spki', format: 'pem' })
  const manifest = {
    schemaVersion: 1,
    product: 'PsychoVault',
    repository: 'Psycho-Core/PsychoVault',
    platform: 'win32-x64',
    version: '1.0.4',
    tag: 'v1.0.4',
    assetName: 'PsychoVault-Setup-1.0.4.exe',
    sha256: 'b'.repeat(64),
    size: 123456,
    keyId: keyIdForPublicKey(publicPem)
  }
  const bytes = Buffer.from(JSON.stringify(manifest))
  const signature = crypto.sign(null, bytes, privateKey).toString('base64')
  const changed = Buffer.from(bytes)
  changed[changed.indexOf(Buffer.from('1.0.4'))] ^= 1
  assert.equal(verifyUpdateManifest(changed, signature, publicPem, { currentVersion: '1.0.3', tag: 'v1.0.4' }).reason, 'signature-invalid')
  assert.equal(verifyUpdateManifest(bytes, 'not a signature', publicPem, { currentVersion: '1.0.3', tag: 'v1.0.4' }).ok, false)
  assert.equal(verifyUpdateManifest(bytes, signature, publicPem, { currentVersion: '1.0.3', tag: 'v1.0.5' }).reason, 'tag-mismatch')
  assert.equal(verifyUpdateManifest(bytes, signature, publicPem, { currentVersion: '1.0.3', tag: 'v1.0.4', platform: 'linux-x64' }).reason, 'platform')
})

test('only considers newer stable semantic versions', () => {
  assert.equal(compareVersions('v1.0.4', '1.0.3'), 1)
  assert.equal(compareVersions('1.1.0', '1.0.99'), 1)
  assert.equal(compareVersions('1.0.3', 'v1.0.3'), 0)
  assert.equal(compareVersions('1.0.2', '1.0.3'), -1)
  assert.equal(compareVersions('latest', '1.0.3'), null)
  assert.equal(installerDownloadUrl({ version: '../1.0.4', tag: 'v../1.0.4', assetName: 'evil.exe' }), null)
})

test('a 404 from the releases API is reported as a private repository, not a network problem', () => {
  const c = classifyCheckFailure(404)
  assert.equal(c.reason, 'repository-private')
  assert.equal(c.status, 404)
  assert.equal(c.retryable, false)
})

test('a failure with no status is classified as offline and is retryable', () => {
  const c = classifyCheckFailure(0, 'fetch failed')
  assert.equal(c.reason, 'offline')
  assert.equal(c.retryable, true)
  assert.equal(c.message, 'fetch failed')
})

test('server and rate-limit responses stay retryable, client errors do not', () => {
  assert.equal(classifyCheckFailure(500).retryable, true)
  assert.equal(classifyCheckFailure(503).retryable, true)
  assert.equal(classifyCheckFailure(429).retryable, true)
  assert.equal(classifyCheckFailure(403).retryable, false)
  assert.equal(classifyCheckFailure(403).reason, 'release-check-unavailable')
})
