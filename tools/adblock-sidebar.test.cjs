'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const read = (p) => fs.readFileSync(path.join(__dirname, '..', p), 'utf8')
const html = read('app/ui/index.html')
const js = read('app/ui/renderer.js')

test('ad blocker button sits above Proton and Settings', () => {
  assert.ok(html.indexOf('id="btn-adblock"') < html.indexOf('id="btn-proton"'))
  assert.ok(html.indexOf('id="btn-proton"') < html.indexOf('id="btn-settings"'))
  assert.match(html, /id="btn-adblock"[^>]*aria-pressed="true"/)
})

test('button toggles the existing blocker without pretending it is uBlock', async () => {
  const start = js.indexOf("$('#btn-adblock').onclick =")
  const end = js.indexOf("$('#btn-proton').onclick", start)
  let patch, message
  const btn = {}
  const state = { settings: { blockAds: true } }
  vm.runInNewContext(js.slice(start, end), { $: () => btn, state, window: { pv: { settings: { set: async p => { patch = p } } } }, toast: m => { message = m } })
  await btn.onclick(); assert.equal(patch.blockAds, false); assert.match(message, /off/)
  state.settings.blockAds = false
  await btn.onclick(); assert.equal(patch.blockAds, true); assert.match(message, /on/)
  assert.match(js, /adblock\.setAttribute\('aria-pressed'/)
})
