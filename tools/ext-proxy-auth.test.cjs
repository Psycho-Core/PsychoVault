'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { matchesProxyChallenge, allowsProxyAuth } = require('../app/ext-proxy-auth')
test('only allows the exact proxy endpoint for proxy challenges', () => {
  const value = { mode: 'fixed_servers', rules: { singleProxy: { host: 'vpn.example', port: 4443 } } }
  assert.equal(matchesProxyChallenge(value, { isProxy: true, host: 'VPN.EXAMPLE', port: 4443 }), true)
  assert.equal(matchesProxyChallenge(value, { isProxy: false, host: 'vpn.example', port: 4443 }), false)
  assert.equal(matchesProxyChallenge(value, { isProxy: true, host: 'evil.example', port: 4443 }), false)
  assert.equal(matchesProxyChallenge(value, { isProxy: true, host: 'vpn.example', port: 443 }), false)
  assert.equal(matchesProxyChallenge({ mode: 'system' }, { isProxy: true, host: 'vpn.example', port: 4443 }), false)
})


test('auth bridge requires broad host and webRequest auth permissions', () => {
  const permissions = ['webRequest', 'webRequestAuthProvider']
  assert.equal(allowsProxyAuth({ permissions, host_permissions: ['<all_urls>'] }), true)
  assert.equal(allowsProxyAuth({ permissions, host_permissions: ['http://*/', 'https://*/'] }), true)
  assert.equal(allowsProxyAuth({ permissions, host_permissions: ['https://example.com/*'] }), false)
  assert.equal(allowsProxyAuth({ permissions: ['webRequest'], host_permissions: ['<all_urls>'] }), false)
})
