'use strict'
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('node:fs'), os = require('node:os'), path = require('node:path')
const { cleanupBackup } = require('../app/update-backup')
test('backup cleanup requires matching version and exact sibling directory', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-backup-')), profile = path.join(root, 'profile'), dir = path.join(root, 'app'), backup = dir + '.pv-update-backup'
  fs.mkdirSync(profile); fs.mkdirSync(dir); fs.mkdirSync(backup)
  const marker = path.join(profile, 'update-backup.json'), executable = path.join(dir, 'PsychoVault.exe')
  fs.writeFileSync(marker, JSON.stringify({ version: '1.2.3', install: dir, backup: root }))
  assert.equal(await cleanupBackup(profile, executable, '1.2.3'), false)
  fs.writeFileSync(marker, JSON.stringify({ version: '1.2.3', install: dir, backup }))
  assert.equal(await cleanupBackup(profile, executable, '1.2.4'), false)
  assert.equal(await cleanupBackup(profile, executable, '1.2.3'), true)
  assert.equal(fs.existsSync(backup), false)
  fs.rmSync(root, { recursive: true, force: true })
})
