'use strict'
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('node:fs'), os = require('node:os'), path = require('node:path')
const { atomic, recover, migrate } = require('../app/vault-migration')
test('failed and interrupted migrations restore both old files', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-vault-migration-')); t.after(() => fs.rmSync(dir, { recursive:true, force:true }))
  fs.writeFileSync(path.join(dir, 'vault.bin'), 'old encrypted vault'); fs.writeFileSync(path.join(dir, 'vault.key.json'), 'old metadata')
  assert.throws(() => migrate(dir, () => { atomic(path.join(dir, 'vault.bin'), 'new'); throw Error('failure') }))
  assert.equal(fs.readFileSync(path.join(dir, 'vault.bin'), 'utf8'), 'old encrypted vault')
  fs.writeFileSync(path.join(dir, 'vault-migration.json'), JSON.stringify({version:1, vault: Buffer.from('old encrypted vault').toString('base64'), key: Buffer.from('old metadata').toString('base64')}))
  fs.writeFileSync(path.join(dir, 'vault.key.json'), 'changed'); assert.equal(recover(dir), true)
  assert.equal(fs.readFileSync(path.join(dir, 'vault.key.json'), 'utf8'), 'old metadata')
})
