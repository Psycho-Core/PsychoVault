'use strict'
// Proton VPN's Sign in button opens the login page with chrome.tabs.create. Electron does not
// implement it, and the extension treats a missing tabs object as a broken install:
//   "Some permissions for the extension are missing, please try to re-install"
// so the login page never opened. These checks hold the replacement in place.
const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const root = path.join(__dirname, '..')
const shim = fs.readFileSync(path.join(root, 'app', 'ext-main-world.js'), 'utf8')
const main = fs.readFileSync(path.join(root, 'app', 'main.js'), 'utf8')

test('the shim supplies chrome.tabs instead of leaving Chromium stub-less', () => {
  assert.match(shim, /tabsInstalled = override\('tabs', tabsApi\)/)
  // the call Proton makes first
  assert.match(shim, /create: function \(details, cb\)/)
  assert.match(shim, /b\.tabsCreate\(details\)/)
  assert.match(shim, /query: function \(q, cb\)/)
  assert.match(shim, /b\.tabsQuery\(q\)/)
})

test('the events Proton listens on exist and are not shared between installs', () => {
  assert.match(shim, /onRemoved: tabRemoved,\n\s*onCreated: tabCreated,\n\s*onUpdated: tabUpdated/)
  assert.match(shim, /b\.onTabsRemoved\(function \(tabId\) \{ tabRemoved\.__emit/)
})

test('every tabs channel is in the allow-list and answered in main', () => {
  const allow = main.slice(main.indexOf('const EXT_SHIM_CHANNELS'), main.indexOf(']', main.indexOf('const EXT_SHIM_CHANNELS')))
  for (const ch of ['extshim:tabs:query', 'extshim:tabs:create', 'extshim:tabs:update', 'extshim:tabs:remove']) {
    assert.ok(allow.includes("'" + ch + "'"), ch + ' is not registered')
    assert.ok(main.includes("channel === '" + ch + "'"), ch + ' has no handler')
  }
})

test('a tab opened by an extension is a real tab in the real window', () => {
  const at = main.indexOf("channel === 'extshim:tabs:create'")
  const block = main.slice(at, at + 900)
  assert.match(block, /w\.createTab\(target/, 'the handler does not create a tab')
  assert.match(block, /shimTabInfo\(w, tab\.id\)/, 'the handler does not report the tab back')
})

test('closing a tab tells open extension pages', () => {
  assert.match(main, /function shimAnnounceTabRemoved/)
  assert.match(main, /shimAnnounceTabRemoved\(this, id\)/)
  const preload = fs.readFileSync(path.join(root, 'app', 'ext-shim.js'), 'utf8')
  assert.match(preload, /ipcRenderer\.on\('extshim:tabs:removed'/)
})

test('an extension cannot open javascript: or chrome: urls through the shim', () => {
  const block = main.slice(main.indexOf("channel === 'extshim:tabs:create'"), main.indexOf("channel === 'extshim:tabs:update'"))
  assert.match(block, /if \(!\/\^\(https\?\|ftp\|file\|about\|psychovault\|chrome-extension\):/)
})
