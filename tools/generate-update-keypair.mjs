#!/usr/bin/env node
// Generate the Ed25519 key pair used to authenticate release update manifests.
// The private key must be written outside this repository and stored only as a
// GitHub Actions secret. The public key is embedded in the installed application.
import crypto from 'node:crypto'
import fs from 'node:fs'
import path from 'node:path'

const ROOT = path.resolve(import.meta.dirname, '..')
const publicPath = path.join(ROOT, 'app', 'update-public-key.pem')
const privatePathArg = process.argv[2]
const publicPathArg = process.argv[3] || publicPath

if (!privatePathArg) {
  console.error('Usage: node tools/generate-update-keypair.mjs <private-key-output-outside-repo> [public-key-output]')
  process.exit(2)
}

const privatePath = path.resolve(privatePathArg)
const targetPublicPath = path.resolve(publicPathArg)
if (privatePath === ROOT || privatePath.startsWith(ROOT + path.sep)) {
  console.error('Refusing to write a signing private key inside the repository.')
  process.exit(2)
}
if (targetPublicPath === ROOT || !targetPublicPath.startsWith(ROOT + path.sep)) {
  console.error("The public key must be written inside the repository.")
  process.exit(2)
}
if (fs.existsSync(targetPublicPath)) {
  console.error(`A public update key already exists at ${targetPublicPath}; refusing to replace it.`)
  process.exit(2)
}
if (fs.existsSync(privatePath)) {
  console.error(`Private-key destination already exists: ${privatePath}`)
  process.exit(2)
}

const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519', {
  publicKeyEncoding: { type: 'spki', format: 'pem' },
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' }
})
fs.mkdirSync(path.dirname(privatePath), { recursive: true })
fs.mkdirSync(path.dirname(targetPublicPath), { recursive: true })
fs.writeFileSync(privatePath, privateKey, { encoding: 'utf8', mode: 0o600, flag: 'wx' })
try {
  fs.chmodSync(privatePath, 0o600)
  fs.writeFileSync(targetPublicPath, publicKey, { encoding: 'utf8', mode: 0o644, flag: 'wx' })
} catch (error) {
  try { fs.rmSync(privatePath, { force: true }) } catch { /* keep the repository key pairing atomic */ }
  throw error
}

console.log(`Public key written: ${targetPublicPath}`)
console.log(`Private key written outside the repository with mode 0600: ${privatePath}`)
console.log('Never commit or publish the private key. Store it as the UPDATE_SIGNING_PRIVATE_KEY_B64 Actions secret.')
