'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { matchesProxyChallenge } = require('../app/ext-proxy-auth')
test('only allows the exact proxy endpoint for proxy challenges', () => {
  const value = { mode: 'fixed_servers', rules: { singleProxy: { host: 'vpn.example', port: 4443 } } }
  assert.equal(matchesProxyChallenge(value, { isProxy: true, host: 'VPN.EXAMPLE', port: 4443 }), true)
  assert.equal(matchesProxyChallenge(value, { isProxy: false, host: 'vpn.example', port: 4443 }), false)
  assert.equal(matchesProxyChallenge(value, { isProxy: true, host: 'evil.example', port: 4443 }), false)
  assert.equal(matchesProxyChallenge(value, { isProxy: true, host: 'vpn.example', port: 443 }), false)
  assert.equal(matchesProxyChallenge({ mode: 'system' }, { isProxy: true, host: 'vpn.example', port: 4443 }), false)
})
