'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { validEngine, shortcutSearch } = require('../app/custom-search')
const github = { shortcut: 'gh', name: 'GitHub', url: 'https://github.com/search?q=%s' }
test('shortcuts encode queries and leave other searches alone', () => {
  assert.equal(shortcutSearch('GH a & b', [github]), 'https://github.com/search?q=a%20%26%20b')
  assert.equal(shortcutSearch('git a', [github]), null)
})
test('search templates must be HTTPS and cannot change their origin', () => {
  assert.equal(validEngine(github), true)
  assert.equal(validEngine({ ...github, url: 'https://%s.example.com/' }), false)
  assert.equal(validEngine({ ...github, url: 'http://example.com/?q=%s' }), false)
  assert.equal(validEngine({ ...github, url: 'https://user:pass@example.com/?q=%s' }), false)
})
