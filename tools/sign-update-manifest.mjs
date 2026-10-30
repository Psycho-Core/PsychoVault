#!/usr/bin/env node
// Build the signed release metadata consumed by app/auto-updater.js.
// UPDATE_SIGNING_PRIVATE_KEY_B64 is a repository Actions secret, never a file in Git.
import crypto from 'node:crypto'
import fs from 'node:fs'
import path from 'node:path'
import { createRequire } from 'node:module'
import { keyIdForPublicKey, PLATFORM, PRODUCT, REPOSITORY } from '../app/update-manifest.js'

const ROOT = path.resolve(import.meta.dirname, '..')
const require = createRequire(import.meta.url)
const version = require('../app/package.json').version
const installerPath = path.resolve(process.argv[2] || '')
const assetName = `${PRODUCT}-Setup-${version}.exe`
const expectedInstaller = path.join(ROOT, assetName)
const repository = process.env.GITHUB_REPOSITORY || REPOSITORY
// A local build can sign with the same key the workflow uses, from a file kept out of Git:
// update-private-key.pem (a PEM, or a base64 private key) next to this script's parent folder.
function localKeyFile() {
  for (const name of ['update-private-key.pem', 'update-private-key.b64', 'update-private-key']) {
    const file = path.join(ROOT, name)
    try {
      if (fs.existsSync(file)) return fs.readFileSync(file, 'utf8').trim()
    } catch { /* unreadable, try the next name */ }
  }
  return ''
}
const rawSecret = String(process.env.UPDATE_SIGNING_PRIVATE_KEY_B64 || localKeyFile()).trim().replace(/^["']|["']$/g, '')

if (repository !== REPOSITORY) throw new Error(`Refusing to sign for unexpected repository: ${repository}`)
if (!process.argv[2] || installerPath !== expectedInstaller) {
  throw new Error(`Usage: node tools/sign-update-manifest.mjs ${assetName}`)
}
if (!fs.existsSync(installerPath) || !fs.statSync(installerPath).isFile()) throw new Error(`Installer not found: ${installerPath}`)
if (!rawSecret) throw new Error('Missing UPDATE_SIGNING_PRIVATE_KEY_B64, and no update-private-key.pem beside the repo. Add the Ed25519 private key as a GitHub Actions secret, or keep it locally in that file (it is gitignored).')

// Accept a raw PEM, base64-wrapped PEM, base64 PKCS#8 DER, or a base64 32-byte Ed25519 seed.
function parsePrivateKey(value) {
  const candidates = []
  const unescaped = value.replace(/\\n/g, '\n')
  if (unescaped.includes('-----BEGIN')) candidates.push({ key: unescaped, format: 'pem' })
  const compact = value.replace(/\s+/g, '')
  if (/^[A-Za-z0-9+/_-]+={0,2}$/.test(compact)) {
    const bytes = Buffer.from(compact.replace(/-/g, '+').replace(/_/g, '/'), 'base64')
    const text = bytes.toString('utf8')
    if (text.includes('-----BEGIN')) candidates.push({ key: text.replace(/\\n/g, '\n'), format: 'pem' })
    candidates.push({ key: bytes, format: 'der', type: 'pkcs8' })
    if (bytes.length === 32) {
      candidates.push({ key: Buffer.concat([Buffer.from('302e020100300506032b657004220420', 'hex'), bytes]), format: 'der', type: 'pkcs8' })
    }
  }
  for (const candidate of candidates) {
    try { return crypto.createPrivateKey(candidate) } catch {}
  }
  throw new Error('Could not parse UPDATE_SIGNING_PRIVATE_KEY_B64 as an Ed25519 private key (PEM, base64 PEM, or base64 PKCS#8).')
}
const privateKey = parsePrivateKey(rawSecret)
if (privateKey.asymmetricKeyType !== 'ed25519') throw new Error('The update signing key must be an Ed25519 private key.')

const publicPath = path.join(ROOT, 'app', 'update-public-key.pem')
const publicPem = fs.readFileSync(publicPath, 'utf8')
const configuredPublic = crypto.createPublicKey(publicPem).export({ type: 'spki', format: 'der' })
const derivedPublic = crypto.createPublicKey(privateKey).export({ type: 'spki', format: 'der' })
if (!configuredPublic.equals(derivedPublic)) throw new Error('The Actions signing key does not match app/update-public-key.pem.')

const stat = fs.statSync(installerPath)
const hash = crypto.createHash('sha256')
for await (const chunk of fs.createReadStream(installerPath)) hash.update(chunk)
const manifest = {
  schemaVersion: 1,
  product: PRODUCT,
  repository: REPOSITORY,
  platform: PLATFORM,
  version,
  tag: `v${version}`,
  assetName,
  sha256: hash.digest('hex'),
  size: stat.size,
  keyId: keyIdForPublicKey(publicPem)
}
const manifestBytes = Buffer.from(JSON.stringify(manifest, null, 2) + '\n', 'utf8')
const signature = crypto.sign(null, manifestBytes, privateKey)
if (!crypto.verify(null, manifestBytes, crypto.createPublicKey(publicPem), signature)) {
  throw new Error('The generated update signature did not verify against the embedded public key.')
}

fs.writeFileSync(path.join(ROOT, 'update-manifest.json'), manifestBytes)
fs.writeFileSync(path.join(ROOT, 'update-manifest.sig'), signature.toString('base64') + '\n')
console.log(`Signed ${manifest.assetName} for ${manifest.tag} (${manifest.sha256}) with key ${manifest.keyId}.`)
