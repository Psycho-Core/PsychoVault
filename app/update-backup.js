'use strict'
const fs = require('node:fs')
const path = require('node:path')
// Only remove the exact sibling backup recorded by our helper, and only after the new
// version has opened a window and survived the grace period. A bad marker is left alone.
async function cleanupBackup(profile, executable, version) {
  const marker = path.join(profile, 'update-backup.json')
  let entry
  try { entry = JSON.parse((await fs.promises.readFile(marker, 'utf8')).replace(/^\uFEFF/, '')) } catch { return false }
  const dir = path.dirname(executable), backup = dir + '.pv-update-backup'
  const equal = (a, b) => typeof a === 'string' && path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase()
  if (!entry || entry.version !== version || !equal(entry.install, dir) || !equal(entry.backup, backup)) return false
  try {
    const stat = await fs.promises.lstat(backup)
    if (!stat.isDirectory() || stat.isSymbolicLink()) return false
    await fs.promises.rm(backup, { recursive: true, force: false })
    await fs.promises.unlink(marker)
    return true
  } catch { return false }
}
module.exports = { cleanupBackup }
