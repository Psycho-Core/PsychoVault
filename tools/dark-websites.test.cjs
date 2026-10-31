'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const main = fs.readFileSync(path.join(__dirname, '../app/main.js'), 'utf8')
const html = fs.readFileSync(path.join(__dirname, '../app/ui/settings.html'), 'utf8')
const source = main.match(/const darkScript = `([\s\S]*?)`/)[1]
function apply(colour, enabled, existing) {
  let removed = false, inserted = null
  const doc = { body: {}, documentElement: {}, head: { appendChild(el) { inserted = el } },
    getElementById: () => existing ? { remove() { removed = true } } : null,
    createElement: () => ({}) }
  vm.runInNewContext(source.replace('${JSON.stringify(!!settings.darkWebsites)}', String(enabled)), { document: doc, getComputedStyle: () => ({ backgroundColor: colour }) })
  return { inserted, removed }
}
test('darkening only applies to light pages, and turning it off removes the filter', () => {
  assert.match(apply('rgb(255, 255, 255)', true).inserted.textContent, /invert/)
  assert.equal(apply('rgb(10, 15, 20)', true).inserted, null)
  assert.equal(apply('rgb(255, 255, 255)', false, true).removed, true)
  assert.match(html, /id="darkWebsites"/)
})
test('darkening runs only on ordinary HTTP pages', () => {
  const section = main.slice(main.indexOf('  applyEnhancements(tab)'), main.indexOf('  setEnhancements(patch)'))
  assert.match(section, /if \(!\/\^https\?:/)
})
