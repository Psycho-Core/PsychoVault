'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const html = fs.readFileSync(path.join(__dirname, '../app/ui/index.html'), 'utf8')
const js = fs.readFileSync(path.join(__dirname, '../app/ui/renderer.js'), 'utf8')
const start = js.indexOf("$('#btn-proton').onclick =")
const end = js.indexOf("$('#btn-settings').onclick", start)

test('Proton rail button is directly above settings and has a label', () => {
  assert.match(html, /id="btn-proton"[^>]*aria-label="Open Proton VPN"/)
  assert.ok(html.indexOf('id="btn-proton"') < html.indexOf('id="btn-settings"'))
  assert.match(html, /id="btn-proton"[\s\S]*?<\/button>\s*<button class="rail-btn" id="btn-settings"/)
})

test('opens the installed bundled Proton popup and reports disabled or missing', async () => {
  let items = [{ builtin: 'proton-vpn', enabled: true, loaded: true, dir: 'builtin-dir', popup: 'popup.html' }]
  let opened, message
  const button = {}
  vm.runInNewContext(js.slice(start, end), {
    $: () => button,
    window: { pv: { extensions: { list: async () => items, openpage: async (o) => { opened = o; return { ok: true } } } } },
    toast: (text) => { message = text }
  })
  await button.onclick()
  assert.equal(opened.dir, 'builtin-dir'); assert.equal(opened.page, 'popup.html')
  items[0].enabled = false; await button.onclick(); assert.match(message, /Enable Proton/)
  items = []; await button.onclick(); assert.match(message, /not installed/)
})
