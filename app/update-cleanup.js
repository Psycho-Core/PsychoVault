'use strict'
const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')
const MARKER = 'update-installer-cleanup.json'
// Only records the exact installer saved by this updater, never a directory scan.
async function rememberInstaller(userData, downloads, installer, manifest) {
  const name = path.basename(installer)
  if (path.resolve(installer) !== path.resolve(downloads, name) || !/^PsychoVault-Setup-\d+\.\d+\.\d+(?: \(\d+\))?\.exe$/.test(name)) throw new Error('Invalid installer path')
  const marker = path.join(userData, MARKER)
  const temp = marker + '.' + crypto.randomBytes(8).toString('hex') + '.tmp'
  try {
    await fs.promises.writeFile(temp, JSON.stringify({ name, version: manifest.version, sha256: manifest.sha256, size: manifest.size }), { mode: 0o600, flag: 'wx' })
    await fs.promises.rename(temp, marker)
  } finally { await fs.promises.unlink(temp).catch(() => {}) }
}
async function cleanupInstaller(userData, downloads, version) {
  const marker = path.join(userData, MARKER)
  let data
  try { data = JSON.parse(await fs.promises.readFile(marker, 'utf8')) } catch { return false }
  if (!data || data.version !== version || typeof data.name !== 'string' || !/^PsychoVault-Setup-\d+\.\d+\.\d+(?: \(\d+\))?\.exe$/.test(data.name) || !/^[a-f0-9]{64}$/.test(data.sha256 || '') || !Number.isSafeInteger(data.size) || data.size <= 0) return false
  const file = path.join(downloads, data.name)
  try {
    const before = await fs.promises.lstat(file)
    if (!before.isFile() || before.isSymbolicLink() || before.size !== data.size) return false
    const hash = crypto.createHash('sha256')
    await new Promise((resolve, reject) => fs.createReadStream(file).on('data', chunk => hash.update(chunk)).on('error', reject).on('end', resolve))
    if (hash.digest('hex') !== data.sha256) return false
    const after = await fs.promises.lstat(file)
    if (!after.isFile() || after.isSymbolicLink() || before.size !== after.size || before.mtimeMs !== after.mtimeMs || before.ino !== after.ino) return false
    await fs.promises.unlink(file)
    await fs.promises.unlink(marker).catch(() => {})
    return true
  } catch { return false }
}
module.exports = { rememberInstaller, cleanupInstaller }
