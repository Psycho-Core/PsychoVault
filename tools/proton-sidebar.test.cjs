'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const html = fs.readFileSync(path.join(__dirname, '../app/ui/index.html'), 'utf8')
const js = fs.readFileSync(path.join(__dirname, '../app/ui/renderer.js'), 'utf8')
const { inspectProtonPopup, clickProtonPopup } = require('../app/proton-toggle')

test('Proton rail button sits directly above settings', () => {
  assert.match(html, /id="btn-proton"[^>]*aria-label="Open Proton VPN"/)
  assert.match(html, /id="btn-proton"[\s\S]*?<\/button>\s*<button class="rail-btn" id="btn-settings"/)
})

test('left click toggles Proton and right click opens its popup', async () => {
  let opened, count = 0, message = ''
  const buttons = { '#btn-proton': {}, '#btn-adblock': {} }
  const start = js.indexOf('async function openProtonSettings()')
  const end = js.indexOf("$('#btn-settings').onclick", start)
  vm.runInNewContext(js.slice(start, end), {
    $: key => buttons[key],
    window: { pv: { proton: { toggle: async () => { count++; return { ok: true, action: 'connect' } } },
      tab: { new: () => {} }, extensions: { list: async () => [{ builtin: 'proton-vpn', enabled: true, loaded: true, dir: 'builtin-dir', popup: 'popup.html' }], openpage: async o => { opened = o; return { ok: true } } } } },
    toast: text => { message = text }
  })
  await buttons['#btn-proton'].onclick()
  assert.equal(count, 1); assert.match(message, /not verified/)
  buttons['#btn-proton'].oncontextmenu({ preventDefault() {} })
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(opened.dir, 'builtin-dir'); assert.equal(opened.page, 'popup.html')
})

test('popup state and actions use visible Proton controls', () => {
  const elems = {}
  const element = (visible, text) => ({ textContent: text, disabled: false, getClientRects: () => visible ? [{}] : [], click() { this.clicked = true } })
  elems['#logged-view'] = element(true, '')
  elems['#sign-in-view'] = element(false, '')
  elems['#status button.quick-connect-button'] = element(true, 'Connect')
  elems['#status button.disconnection-button'] = element(false, 'Disconnect')
  const ctx = { document: { querySelector: name => elems[name] }, getComputedStyle: () => ({ display: 'block' }) }
  const inspect = vm.runInNewContext('(' + inspectProtonPopup.toString() + ')', ctx)
  const click = vm.runInNewContext('(' + clickProtonPopup.toString() + ')', ctx)
  assert.equal(inspect().state, 'off'); assert.equal(click('connect'), true)
  assert.equal(elems['#status button.quick-connect-button'].clicked, true)
  elems['#status button.disconnection-button'] = element(true, 'Disconnect')
  assert.equal(inspect().state, 'on'); assert.equal(click('disconnect'), true)
  elems['#logged-view'] = element(false, '')
  elems['#sign-in-view'] = element(true, '')
  assert.equal(inspect().state, 'signed-out')
})
