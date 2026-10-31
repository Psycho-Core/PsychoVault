'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const { saveVerifiedInstaller } = require('../app/update-download')

test('saves the verified installer in Downloads without overwriting existing files', async (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-update-'))
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }))
  const name = 'PsychoVault-Setup-1.0.30.exe'
  const temporary = path.join(dir, '.download')
  fs.writeFileSync(path.join(dir, name), 'existing')
  fs.writeFileSync(temporary, 'verified')
  const result = await saveVerifiedInstaller(temporary, dir, name)
  assert.equal(result, path.join(dir, 'PsychoVault-Setup-1.0.30 (1).exe'))
  assert.equal(fs.readFileSync(path.join(dir, name), 'utf8'), 'existing')
  assert.equal(fs.readFileSync(result, 'utf8'), 'verified')
  assert.equal(fs.existsSync(temporary), false)
})

test('rejects untrusted filenames and preserves an unfinished temporary file', async (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-update-'))
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }))
  const temporary = path.join(dir, '.download')
  fs.writeFileSync(temporary, 'data')
  await assert.rejects(saveVerifiedInstaller(temporary, dir, '../evil.exe'), /invalid/)
  assert.equal(fs.readFileSync(temporary, 'utf8'), 'data')
})

test('both update paths verify before saving in Downloads without a Save As dialog', () => {
  const src = fs.readFileSync(path.join(__dirname, '../app/auto-updater.js'), 'utf8')
  assert.doesNotMatch(src, /showInstallerSaveDialog|showSaveDialog/)
  assert.equal((src.match(/await downloadInstaller\(url, temporaryDestination, manifest, onProgress\)/g) || []).length, 2)
  assert.equal((src.match(/await saveVerifiedInstaller\(temporaryDestination, app.getPath\('downloads'\), manifest.assetName\)/g) || []).length, 2)
  for (const part of src.split('await downloadInstaller(url, temporaryDestination, manifest, onProgress)').slice(1)) {
    assert.ok(part.indexOf('saveVerifiedInstaller(') < part.indexOf('launchInstaller('))
  }
})

test('concurrent saves do not overwrite each other', async (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-update-'))
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }))
  const a = path.join(dir, '.one'), b = path.join(dir, '.two')
  fs.writeFileSync(a, 'one')
  fs.writeFileSync(b, 'two')
  const results = await Promise.all([
    saveVerifiedInstaller(a, dir, 'PsychoVault-Setup-1.0.30.exe'),
    saveVerifiedInstaller(b, dir, 'PsychoVault-Setup-1.0.30.exe')
  ])
  assert.notEqual(results[0], results[1])
  assert.deepEqual(results.map(f => fs.readFileSync(f, 'utf8')).sort(), ['one', 'two'])
})
