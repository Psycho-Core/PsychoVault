'use strict'

const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const { LANGUAGES, buildTranslationUrl } = require('../app/translation.js')

test('builds an auto-detect translation URL and preserves the source URL', () => {
  const source = 'https://example.org/articles/bonjour?ref=mail#section-2'
  const result = buildTranslationUrl(source, 'fr')
  assert.equal(result.ok, true)
  assert.equal(result.languageName, 'French')
  const translated = new URL(result.url)
  assert.equal(translated.origin, 'https://translate.google.com')
  assert.equal(translated.pathname, '/translate')
  assert.equal(translated.searchParams.get('sl'), 'auto')
  assert.equal(translated.searchParams.get('tl'), 'fr')
  assert.equal(translated.searchParams.get('u'), source)
})

test('rejects internal, invalid, and credential-bearing URLs', () => {
  assert.match(buildTranslationUrl('psychovault://settings', 'en').error, /web pages/)
  assert.match(buildTranslationUrl('not a URL', 'en').error, /Open a web page/)
  assert.match(buildTranslationUrl('https://user:pass@example.org/private', 'en').error, /credentials/)
  for (const language of ['xx', 'constructor', 'toString', '__proto__']) {
    assert.match(buildTranslationUrl('https://example.org', language).error, /supported translation language/)
  }
})

test('exposes the same 20 supported target languages shown in Settings', () => {
  assert.equal(Object.keys(LANGUAGES).length, 20)
  assert.equal(LANGUAGES.en, 'English')
  assert.equal(LANGUAGES['zh-CN'], 'Chinese (Simplified)')
  const settingsHtml = fs.readFileSync(path.join(__dirname, '../app/ui/settings.html'), 'utf8')
  const select = settingsHtml.match(/<select id="translateTarget"[^>]*>([\s\S]*?)<\/select>/)
  assert.ok(select, 'Settings must provide a translation language selector')
  const options = [...select[1].matchAll(/<option value="([^"]+)"/g)].map((match) => match[1])
  assert.deepEqual(options, Object.keys(LANGUAGES))
})
