'use strict'
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path')
const main = fs.readFileSync(path.join(__dirname, '../app/main.js'), 'utf8')
const html = fs.readFileSync(path.join(__dirname, '../app/ui/settings.html'), 'utf8')
test('legacy dark filter is replaced by bundled Dark Reader', () => {
  assert.doesNotMatch(main, /const darkScript|__pv_dark_websites/)
  assert.doesNotMatch(html, /id="darkWebsites"/)
  assert.match(main, /key: 'dark-reader'/)
  assert.match(html, /Manage the bundled Dark Reader extension/)
})
