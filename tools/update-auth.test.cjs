'use strict'
// Tests for the updater's credential handling. The important case is the redirect: a token
// must reach github.com and must NOT reach the host GitHub redirects downloads to.
const test = require('node:test')
const assert = require('node:assert')
const { shouldKeepAuthorization, authHeader, tokenSource, apiAssetUrl } = require('../app/update-auth.js')

test('the credential is kept on a same-host redirect', () => {
  assert.strictEqual(
    shouldKeepAuthorization(
      'https://api.github.com/repos/a/b/releases/latest',
      'https://api.github.com/repositories/1/releases/latest'
    ),
    true
  )
})

test('the credential is dropped when the redirect changes host', () => {
  assert.strictEqual(
    shouldKeepAuthorization(
      'https://github.com/Psycho-Core/PsychoVault/releases/download/v1.0.9/x.exe',
      'https://release-assets.githubusercontent.com/github-production-release-asset/123?sp=r'
    ),
    false
  )
})

test('a protocol downgrade drops the credential even on the same host', () => {
  assert.strictEqual(shouldKeepAuthorization('https://api.github.com/x', 'http://api.github.com/x'), false)
})

test('an unparseable redirect target drops the credential rather than guessing', () => {
  assert.strictEqual(shouldKeepAuthorization('https://api.github.com/x', 'not a url'), false)
  assert.strictEqual(shouldKeepAuthorization('', ''), false)
})

test('authHeader builds a bearer header and refuses junk', () => {
  assert.deepStrictEqual(authHeader('ghp_abc123'), { Authorization: 'Bearer ghp_abc123' })
  assert.strictEqual(authHeader(''), null)
  assert.strictEqual(authHeader('   '), null)
  assert.strictEqual(authHeader('tok en'), null, 'a space would split the header')
  assert.strictEqual(authHeader('tok\nen'), null, 'a newline is header injection')
  assert.strictEqual(authHeader(undefined), null)
})

test('tokenSource prefers the override, then the bundled token, then none', () => {
  assert.strictEqual(tokenSource('bundled', true), 'settings')
  assert.strictEqual(tokenSource('bundled', false), 'bundled')
  assert.strictEqual(tokenSource('', false), 'none')
  assert.strictEqual(tokenSource(undefined, false), 'none')
})

test('apiAssetUrl only accepts a positive integer asset id', () => {
  assert.ok(apiAssetUrl(691587127).startsWith('https://api.github.com/repos/'))
  assert.strictEqual(apiAssetUrl(0), null)
  assert.strictEqual(apiAssetUrl(-1), null)
  assert.strictEqual(apiAssetUrl('abc'), null)
})
