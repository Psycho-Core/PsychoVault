'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const vm = require('node:vm')
const path = require('node:path')
const src = fs.readFileSync(path.join(__dirname, '../app/ext-shim.js'), 'utf8')
const installer = src.slice(src.indexOf('function installExtensionAPIs'), src.indexOf('const url = currentURL()'))

function page(hostname, pathname, protocol = 'chrome-extension:') {
  let closed = false
  const lines = []
  const bridge = {
    log: line => lines.push(line), report() {}, onTabsRemoved() {},
    tabsCreate: async details => ({ ok: true, tab: { id: 7, url: details.url } })
  }
  const context = { location: { hostname, pathname, protocol }, chrome: {}, bridge,
    close: () => { closed = true }, Promise, Set }
  vm.runInNewContext(installer + '\ninstallExtensionAPIs(globalThis, "bridge")', context)
  return { context, lines, isClosed: () => closed }
}

test('Proton popup close does not interrupt a pending sign-in tab request', async () => {
  const p = page('jplgfhpmjnbigmhklmmbgecoobifkmpa', '/popup.html')
  let finish
  p.context.bridge.tabsCreate = () => new Promise(resolve => { finish = resolve })
  const tab = new Promise(resolve => p.context.chrome.tabs.create({ url: 'https://account.proton.me/authorize' }, resolve))
  p.context.close()
  assert.equal(p.isClosed(), false)
  finish({ ok: true, tab: { id: 7 } })
  assert.equal((await tab).id, 7)
  assert.match(p.lines[0], /keeping its tab open/)
})

test('other extensions and other Proton pages keep their normal close behavior', () => {
  for (const [host, pagePath, scheme] of [
    ['otherextension', '/popup.html', 'chrome-extension:'],
    ['jplgfhpmjnbigmhklmmbgecoobifkmpa', '/onboarding.html', 'chrome-extension:'],
    ['jplgfhpmjnbigmhklmmbgecoobifkmpa', '/popup.html', 'https:']
  ]) {
    const p = page(host, pagePath, scheme)
    p.context.close()
    assert.equal(p.isClosed(), true)
  }
})

test('bundled Proton code schedules close before waiting for its permission request', () => {
  const popup = fs.readFileSync(path.join(__dirname, '../app/builtin/proton-vpn/js/popup.js'), 'utf8')
  assert.match(popup, /setTimeout\(\(\)=>\{[^}]*window\.close\(\)\},200\);\(yield browser\.permissions\.request/)
})

test('Proton permission request succeeds only for permissions Chromium already granted', async () => {
  for (const granted of [true, false]) {
    let requested = 0
    const original = { contains: (_d, cb) => cb(granted), request: () => { requested++ }, getAll() {} }
    const g = { location: { protocol: 'chrome-extension:', hostname: 'jplgfhpmjnbigmhklmmbgecoobifkmpa', pathname: '/popup.html' },
      close() {}, chrome: { permissions: original, runtime: {} }, bridge: { report() {}, onTabsRemoved() {}, log() {} }, Promise, Set }
    vm.runInNewContext(installer + '\ninstallExtensionAPIs(globalThis, "bridge")', g)
    const details = { permissions: ['proxy'], origins: ['http://*/*','https://*/*','ftp://*/*','ws://*/*','wss://*/*','https://account.protonvpn.com/*','https://account.proton.me/*'] }
    const result = await new Promise(resolve => g.chrome.permissions.request(details, resolve))
    assert.equal(result, granted)
    assert.equal(requested, 0)
    g.chrome.permissions.request({ permissions: ['tabs'] }, () => {})
    assert.equal(requested, 1, 'unrelated permissions must go to the original API')
  }
})

test('cached browser polyfill permission facade also checks existing grants', async () => {
  let nativeRequests = 0
  const original = { contains: (_d, cb) => cb(true), request: () => { nativeRequests++ } }
  const cachedBrowser = { permissions: { request: async () => { throw new Error('old native request') } } }
  const g = { location: { protocol: 'chrome-extension:', hostname: 'jplgfhpmjnbigmhklmmbgecoobifkmpa', pathname: '/popup.html' },
    close() {}, chrome: { permissions: original, runtime: {} }, browser: cachedBrowser,
    bridge: { report() {}, onTabsRemoved() {}, log() {} }, Promise, Set }
  vm.runInNewContext(installer + '\ninstallExtensionAPIs(globalThis, "bridge")', g)
  const details = { permissions: ['proxy'], origins: ['http://*/*','https://*/*','ftp://*/*','ws://*/*','wss://*/*','https://account.protonvpn.com/*','https://account.proton.me/*'] }
  assert.equal(await g.browser.permissions.request(details), true)
  assert.equal(nativeRequests, 0)
})
