'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const main = fs.readFileSync(path.join(__dirname, '../app/main.js'), 'utf8')
const code = main.slice(main.indexOf('function shimSenderId('), main.indexOf('// A tab as chrome.tabs describes'))
const get = vm.runInNewContext(code + '\nshimSenderId')

test('uses the actual service worker scope to identify its extension', () => {
  assert.equal(get({ serviceWorker: { scope: 'chrome-extension://abc123/' } }), 'abc123')
  assert.equal(get({ serviceWorker: { scope: 'https://example.com/' }, sender: { scope: 'chrome-extension://fake/' } }), '')
  assert.equal(get({}), '')
})

test('keeps frame identification and rejects non-extension frames', () => {
  assert.equal(get({ senderFrame: { url: 'chrome-extension://real/popup.html' } }), 'real')
  assert.equal(get({ senderFrame: { url: 'https://example.com/' } }), '')
})
