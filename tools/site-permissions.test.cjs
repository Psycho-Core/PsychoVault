'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const { originOf, sitePermission } = require('../app/site-permissions')
const settings = { denyPerms: 'media,geolocation,notifications', sitePermissions: { 'https://example.com': { camera: 'allow', microphone: 'deny', geolocation: 'allow', notifications: 'deny' } } }
test('permission decisions are origin-specific and handle camera and microphone separately', () => {
  assert.equal(sitePermission(settings, 'https://example.com/path', 'media', { mediaTypes: ['video'] }), true)
  assert.equal(sitePermission(settings, 'https://example.com', 'media', { mediaTypes: ['audio'] }), false)
  assert.equal(sitePermission(settings, 'https://example.com', 'media', { mediaTypes: ['audio', 'video'] }), false)
  assert.equal(sitePermission(settings, 'https://other.example.com', 'geolocation', {}), false)
  assert.equal(sitePermission(settings, 'http://example.com', 'geolocation', {}), false)
  assert.equal(sitePermission(settings, 'https://example.com', 'geolocation', {}), true)
  assert.equal(sitePermission(settings, 'https://example.com', 'notifications', {}), false)
})
test('rejects unsafe and unknown origins and unknown media', () => {
  assert.equal(originOf('file:///etc/passwd'), '')
  assert.equal(originOf('https://user:pass@example.com'), '')
  assert.equal(sitePermission(settings, 'data:text/html,x', 'geolocation', {}), false)
  assert.equal(sitePermission(settings, 'https://example.com', 'media', { mediaType: 'screen' }), false)
  assert.equal(sitePermission(settings, 'https://example.com', 'media', {}), false)
})
