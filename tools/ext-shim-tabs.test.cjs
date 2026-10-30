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
const shim = fs.readFileSync(path.join(root, 'app', 'ext-shim.js'), 'utf8')
const main = fs.readFileSync(path.join(root, 'app', 'main.js'), 'utf8')

test('the shim supplies chrome.tabs instead of leaving Chromium stub-less', () => {
  assert.match(shim, /var tabsInstalled = override\('tabs', merge\(\{/)
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
  assert.match(shim, /ipcRenderer\.on\('extshim:tabs:removed'/)
})

test('an extension cannot open javascript: or chrome: urls through the shim', () => {
  const block = main.slice(main.indexOf("channel === 'extshim:tabs:create'"), main.indexOf("channel === 'extshim:tabs:update'"))
  assert.match(block, /if \(!\/\^\(https\?\|ftp\|file\|about\|psychovault\|chrome-extension\):/)
})

test('the overrides keep what Chromium already implements', () => {
  // Replacing chrome.tabs wholesale dropped tabs.sendMessage, tabs.reload and
  // tabs.executeScript, which Electron does implement - an extension using one of those would
  // break for a reason that has nothing to do with the proxy.
  assert.match(shim, /var merge = function \(target, original\)/)
  assert.match(shim, /var tabsInstalled = override\('tabs', merge\(\{/)
  assert.match(shim, /\}, chromeObj\.tabs\)\)/)
  assert.match(shim, /\}, chromeObj\.proxy\)\)/)
  assert.match(shim, /\}, chromeObj\.privacy\)\)/)
})

test('tabs.query does not report the extension page as the active tab', () => {
  // Chrome calls tabs.query from a popup, which is not a tab. Here an extension page is a tab,
  // so it reported itself and the extension saw a chrome-extension:// url where it expected the
  // page the user was looking at.
  const at = main.indexOf("channel === 'extshim:tabs:query'")
  const block = main.slice(at, main.indexOf("channel === 'extshim:tabs:create'"))
  assert.match(block, /list\.filter\(\(t\) => !String\(t\.url \|\| ''\)\.startsWith\('chrome-extension:\/\/'\)\)/)
})

test('an extension page console line reaches the shim log', () => {
  // A blank popup has no other evidence: no devtools here, and nothing on disk.
  assert.match(main, /function watchExtensionConsole\(wc\)/)
  assert.match(main, /wc\.on\('console-message'/)
  assert.match(main, /if \(!pageUrl\.startsWith\('chrome-extension:\/\/'\)\) return/)
  assert.match(main, /watchExtensionConsole\(wc\)/)
})
