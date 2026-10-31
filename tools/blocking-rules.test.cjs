'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { hostname, parseHosts, siteExcluded, isDomainBlocked } = require('../app/blocking-rules')
test('domain list blocks subdomains and leaves unrelated names alone', () => {
  const domains = new Set(parseHosts('ads.example.com\ntracker.test'))
  assert.equal(isDomainBlocked(domains, 'https://x.ads.example.com/path'), true)
  assert.equal(isDomainBlocked(domains, 'https://notads.example.com'), false)
  assert.equal(isDomainBlocked(domains, 'https://tracker.test'), true)
})
test('site exemptions use the exact top-level host', () => {
  assert.equal(siteExcluded('example.com', 'https://example.com/x'), true)
  assert.equal(siteExcluded('example.com', 'https://foo.example.com/x'), false)
  assert.equal(siteExcluded('example.com', 'https://evil-example.com/x'), false)
  assert.equal(hostname('https://user:secret@example.com/'), '')
})
