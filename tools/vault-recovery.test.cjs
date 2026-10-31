'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const vm = require('node:vm')
const crypto = require('node:crypto')
const source = fs.readFileSync(path.join(__dirname, '../app/main.js'), 'utf8')
const segment = (a, b) => source.slice(source.indexOf(a), source.indexOf(b, source.indexOf(a)))
function fixture(method = 'keychain') {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-vault-test-'))
  const context = vm.createContext({ fs, path, crypto, Buffer, USER: dir, VAULT_FILE: 'vault.bin', VAULT_KEY: 'vault.key.json',
    safeStorage: { decryptString: b => b.toString('utf8') }, vaultMethod: () => method,
    readJSON: file => JSON.parse(fs.readFileSync(path.join(dir, file))), vaultArmLock: () => {},
    vaultKey: null, vaultCache: null, vaultLocked: true })
  const code = segment('function vaultDerive(', 'function vaultWrite()') + segment('function vaultLoad()', 'function vaultArmLock()') + segment('function vaultUnlock(', 'const VAULT_WORDS')
  vm.runInContext(code, context)
  return { dir, context }
}

test('keychain vault corruption does not become an empty writable vault', () => {
  const { dir, context } = fixture()
  fs.writeFileSync(path.join(dir, 'vault.bin'), 'corrupt')
  const result = context.vaultUnlock()
  assert.equal(result.ok, false)
  assert.match(result.reason, /left unchanged/)
  assert.equal(context.vaultLocked, true)
  assert.equal(fs.readFileSync(path.join(dir, 'vault.bin'), 'utf8'), 'corrupt')
})

test('wrong-shaped vault is rejected and a missing vault is treated as new', () => {
  const { dir, context } = fixture()
  assert.equal(context.vaultUnlock().ok, true)
  context.vaultLocked = true; context.vaultCache = null
  fs.writeFileSync(path.join(dir, 'vault.bin'), '{}')
  assert.equal(context.vaultUnlock().ok, false)
  assert.equal(context.vaultLocked, true)
})

test('master verifier succeeds but a damaged encrypted vault stays locked', () => {
  const { dir, context } = fixture('master')
  const salt = crypto.randomBytes(16).toString('base64')
  const key = context.vaultDerive('example password', salt)
  fs.writeFileSync(path.join(dir, 'vault.key.json'), JSON.stringify({ salt, check: context.vaultSeal('{"v":1}', key).toString('base64') }))
  fs.writeFileSync(path.join(dir, 'vault.bin'), 'damaged')
  const result = context.vaultUnlock('example password')
  assert.equal(result.ok, false)
  assert.equal(context.vaultLocked, true)
  assert.equal(context.vaultKey, null)
  assert.equal(fs.readFileSync(path.join(dir, 'vault.bin'), 'utf8'), 'damaged')
})

test('unlocking a valid master vault retains its entries', () => {
  const { dir, context } = fixture('master')
  const salt = crypto.randomBytes(16).toString('base64')
  const key = context.vaultDerive('example password', salt)
  fs.writeFileSync(path.join(dir, 'vault.key.json'), JSON.stringify({ salt, check: context.vaultSeal('{"v":1}', key).toString('base64') }))
  fs.writeFileSync(path.join(dir, 'vault.bin'), context.vaultSeal(JSON.stringify([{ site: 'example.com', pass: 'secret' }]), key))
  assert.equal(context.vaultUnlock('wrong').ok, false)
  assert.equal(context.vaultUnlock('example password').ok, true)
  assert.equal(context.vaultCache[0].site, 'example.com')
})

test('master-password operations require an unlocked vault and password fields are masked', () => {
  const settings = fs.readFileSync(path.join(__dirname, '../app/ui/settings.html'), 'utf8')
  for (const id of ['vpw', 'vnew', 'ePass']) assert.match(settings, new RegExp('<input type="password"[^>]*id="' + id + '"'))
  const setMaster = segment("ipcMain.handle('vault:setMaster'", "ipcMain.handle('vault:forgetMaster'")
  const forget = segment("ipcMain.handle('vault:forgetMaster'", "ipcMain.handle('settings:get'")
  assert.match(setMaster, /vaultLocked.*unlock the existing vault first/)
  assert.match(forget, /vaultLocked.*unlock the vault first/)
  assert.match(forget, /!vaultSafeOk\(\)/)
})
