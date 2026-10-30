'use strict'

const crypto = require('node:crypto')

const PRODUCT = 'PsychoVault'
const REPOSITORY = 'Psycho-Core/PsychoVault'
const PLATFORM = 'win32-x64'
const MAX_MANIFEST_BYTES = 64 * 1024
const MAX_INSTALLER_BYTES = 2 * 1024 * 1024 * 1024

function stableVersionParts(value) {
  const match = /^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.exec(String(value || ''))
  return match ? match.slice(1).map(Number) : null
}

function compareVersions(left, right) {
  const a = stableVersionParts(left)
  const b = stableVersionParts(right)
  if (!a || !b) return null
  for (let i = 0; i < 3; i++) {
    if (a[i] !== b[i]) return a[i] > b[i] ? 1 : -1
  }
  return 0
}

function keyIdForPublicKey(publicKeyInput) {
  const publicKey = publicKeyInput && publicKeyInput.type === 'public'
    ? publicKeyInput
    : crypto.createPublicKey(publicKeyInput)
  const der = publicKey.export({ type: 'spki', format: 'der' })
  return crypto.createHash('sha256').update(der).digest('hex')
}

function failure(reason) {
  return { ok: false, reason }
}

function verifyUpdateManifest(rawManifest, signatureText, publicKeyPem, expected = {}) {
  const bytes = Buffer.isBuffer(rawManifest) ? rawManifest : Buffer.from(String(rawManifest || ''), 'utf8')
  if (!bytes.length || bytes.length > MAX_MANIFEST_BYTES) return failure('manifest-size')

  let manifest
  try { manifest = JSON.parse(bytes.toString('utf8')) }
  catch { return failure('manifest-json') }
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) return failure('manifest-shape')

  let publicKey
  let keyId
  try {
    publicKey = crypto.createPublicKey(publicKeyPem)
    keyId = keyIdForPublicKey(publicKey)
  } catch { return failure('public-key') }
  if (manifest.keyId !== keyId) return failure('unknown-key')

  const encodedSignature = String(signatureText || '').trim()
  if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(encodedSignature)) return failure('signature-format')
  const signature = Buffer.from(encodedSignature, 'base64')
  if (signature.length !== 64 || signature.toString('base64') !== encodedSignature) return failure('signature-format')
  try {
    if (!crypto.verify(null, bytes, publicKey, signature)) return failure('signature-invalid')
  } catch { return failure('signature-invalid') }

  if (manifest.schemaVersion !== 1) return failure('schema-version')
  if (manifest.product !== PRODUCT || manifest.repository !== REPOSITORY) return failure('repository')
  if (manifest.platform !== (expected.platform || PLATFORM)) return failure('platform')
  const parts = stableVersionParts(manifest.version)
  if (!parts || manifest.tag !== `v${manifest.version}`) return failure('version')
  if (expected.tag && manifest.tag !== expected.tag) return failure('tag-mismatch')
  if (expected.currentVersion) {
    const comparison = compareVersions(manifest.version, expected.currentVersion)
    if (comparison === null || comparison <= 0) return failure('not-newer')
  }
  if (manifest.assetName !== `${PRODUCT}-Setup-${manifest.version}.exe`) return failure('asset-name')
  if (typeof manifest.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(manifest.sha256)) return failure('sha256-format')
  if (!Number.isSafeInteger(manifest.size) || manifest.size <= 0 || manifest.size > MAX_INSTALLER_BYTES) return failure('asset-size')

  return { ok: true, manifest }
}

// Why an update check failed, in terms the user can act on. A 404 from the releases API is
// what GitHub answers to an unauthenticated caller when the repository is private, and no
// amount of retrying changes that; a timeout is a different problem with a different fix.
function classifyCheckFailure(status, message) {
  const code = Number(status) || 0
  if (code === 404) return { reason: 'repository-private', status: code, retryable: false }
  if (!code) return { reason: 'offline', status: 0, retryable: true, message: String(message || '') }
  return { reason: 'release-check-unavailable', status: code, retryable: code >= 500 || code === 429 }
}

function installerDownloadUrl(manifest) {
  if (!manifest || !stableVersionParts(manifest.version) || manifest.tag !== `v${manifest.version}` ||
      manifest.assetName !== `${PRODUCT}-Setup-${manifest.version}.exe`) return null
  return `https://github.com/${REPOSITORY}/releases/download/${encodeURIComponent(manifest.tag)}/${encodeURIComponent(manifest.assetName)}`
}

module.exports = {
  PRODUCT,
  classifyCheckFailure,
  REPOSITORY,
  PLATFORM,
  stableVersionParts,
  compareVersions,
  keyIdForPublicKey,
  verifyUpdateManifest,
  installerDownloadUrl
}
