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
const secret = String(process.env.UPDATE_SIGNING_PRIVATE_KEY_B64 || '').replace(/\s+/g, '')

if (repository !== REPOSITORY) throw new Error(`Refusing to sign for unexpected repository: ${repository}`)
if (!process.argv[2] || installerPath !== expectedInstaller) {
  throw new Error(`Usage: node tools/sign-update-manifest.mjs ${assetName}`)
}
if (!fs.existsSync(installerPath) || !fs.statSync(installerPath).isFile()) throw new Error(`Installer not found: ${installerPath}`)
if (!secret) throw new Error('Missing UPDATE_SIGNING_PRIVATE_KEY_B64. Add the Ed25519 private key as a GitHub Actions repository secret.')
if (!/^[A-Za-z0-9+/]+={0,2}$/.test(secret)) throw new Error('UPDATE_SIGNING_PRIVATE_KEY_B64 is not valid base64.')

const privatePem = Buffer.from(secret, 'base64').toString('utf8')
let privateKey
try { privateKey = crypto.createPrivateKey(privatePem) }
catch { throw new Error('Could not parse UPDATE_SIGNING_PRIVATE_KEY_B64 as a PEM private key.') }
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
