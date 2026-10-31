'use strict'
const test = require('node:test'), assert = require('node:assert/strict')
const { encryptBackup, decryptBackup, parseCSV, prepareEntries } = require('../app/vault-transfer')
test('encrypted backup roundtrip and wrong password/tampering rejection', () => {
  const entries = [{ site: 'example.com', user: 'u', pass: 'secret' }]
  const backup = encryptBackup(entries, 'long backup password')
  assert.equal(backup.includes('secret'), false)
  assert.deepEqual(decryptBackup(backup, 'long backup password'), entries)
  assert.throws(() => decryptBackup(backup, 'wrong password'))
  const obj = JSON.parse(backup); obj.tag = Buffer.alloc(16).toString('base64')
  assert.throws(() => decryptBackup(JSON.stringify(obj), 'long backup password'))
})
test('CSV quoting and duplicate rejection do not overwrite existing entries', () => {
  const data = parseCSV('url,username,password,notes\r\nhttps://example.com,u,"a,b","two\nlines"\r\nhttps://example.com,u,x,\r\n')
  assert.equal(data[0].pass, 'a,b'); assert.equal(data[0].note, 'two\nlines')
  const result = prepareEntries(data, [{ site: 'example.com', user: 'u', pass: 'old' }])
  assert.equal(result.accepted.length, 0); assert.equal(result.skipped, 2)
  assert.throws(() => parseCSV('url,password\nhttps://x,"bad'))
})
