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
