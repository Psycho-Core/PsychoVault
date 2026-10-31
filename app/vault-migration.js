'use strict'
// A migration touches two files. The journal contains only the previous encrypted
// vault and key metadata, and restores both if the process dies mid-migration.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto')
function atomic(file, bytes) {
  const temp = file + '.' + crypto.randomBytes(8).toString('hex') + '.tmp'
  try { fs.writeFileSync(temp, bytes, { flag: 'wx', mode: 0o600 }); fs.renameSync(temp, file) } finally { try { fs.unlinkSync(temp) } catch { /* already renamed */ } }
}
function recover(dir) {
  const journal = path.join(dir, 'vault-migration.json')
  if (!fs.existsSync(journal)) return false
  const saved = JSON.parse(fs.readFileSync(journal, 'utf8'))
  if (!saved || saved.version !== 1 || !['string','object'].includes(typeof saved.vault) || !['string','object'].includes(typeof saved.key)) throw new Error('Vault migration journal is invalid; no files were changed.')
  for (const [name, value] of [['vault.bin', saved.vault], ['vault.key.json', saved.key]]) {
    const target = path.join(dir, name)
    if (value === null) { try { fs.unlinkSync(target) } catch (e) { if (e.code !== 'ENOENT') throw e } }
    else if (typeof value === 'string') atomic(target, Buffer.from(value, 'base64'))
    else throw new Error('Invalid vault migration journal.')
  }
  fs.unlinkSync(journal)
  return true
}
function migrate(dir, callback) {
  const journal = path.join(dir, 'vault-migration.json')
  if (fs.existsSync(journal)) throw new Error('An earlier vault migration needs recovery first.')
  const read = name => { try { return fs.readFileSync(path.join(dir, name)).toString('base64') } catch (e) { if (e.code === 'ENOENT') return null; throw e } }
  atomic(journal, JSON.stringify({ version: 1, vault: read('vault.bin'), key: read('vault.key.json') }))
  try { callback(); fs.unlinkSync(journal) } catch (err) { try { recover(dir) } catch (recovery) { throw new Error('Migration failed and recovery needs attention: ' + recovery.message) } throw err }
}
module.exports = { atomic, recover, migrate }
