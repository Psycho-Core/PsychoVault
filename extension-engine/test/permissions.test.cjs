'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { parseManifest } = require('../manifest');
const { missingPermissions, contextAllowed, matchPattern, hasHostAccess, allowsProxyAuth } = require('../permissions');

const proton = parseManifest(fs.readFileSync(path.resolve(__dirname, '../../app/builtin/proton-vpn/manifest.json'), 'utf8')).requirements;

test('Proton has the first-slice proxy/auth permissions, but content scripts do not get them', () => {
  assert.deepEqual(missingPermissions(proton, 'proxy.settings.set'), []);
  assert.deepEqual(missingPermissions(proton, 'webRequest.onAuthRequired.addListener'), []);
  assert.equal(contextAllowed('proxy.settings.set', 'worker'), true);
  assert.equal(contextAllowed('proxy.settings.set', 'content'), false);
  assert.equal(contextAllowed('webRequest.onAuthRequired.addListener', 'popup'), false);
});

test('unknown methods and missing optional grants are default-deny', () => {
  const req = parseManifest(JSON.stringify({ manifest_version: 3, name: 'x', version: '1', optional_permissions: ['contextMenus'] })).requirements;
  assert.deepEqual(missingPermissions(req, 'browser.makeCoffee'), ['unknown-method']);
  assert.deepEqual(missingPermissions(req, 'proxy.settings.set'), ['proxy']);
  assert.deepEqual(missingPermissions(req, 'storage.local.get'), ['storage']);
  assert.deepEqual(missingPermissions(req, 'storage.local.get', ['storage']), []);
});

test('host matching is origin-scoped and does not leak across schemes or sibling hosts', () => {
  assert.equal(matchPattern('https://*/', 'https://example.com/path'), true);
  assert.equal(matchPattern('https://*/*', 'https://example.com/path', { ignorePath: true }), true);
  assert.equal(matchPattern('https://*.example.com/*', 'https://a.example.com/'), true);
  assert.equal(matchPattern('https://*.example.com/*', 'https://example.com/'), true);
  assert.equal(matchPattern('https://*.example.com/*', 'https://evil-example.com/'), false);
  assert.equal(matchPattern('https://example.com/*', 'http://example.com/'), false);
  assert.equal(matchPattern('*://*/*', 'ftp://example.com/'), false);
  assert.equal(matchPattern('https://example.com/*', 'https://user:pass@example.com/'), false);
  assert.equal(hasHostAccess(proton, 'https://account.proton.me/login'), true);
  assert.equal(hasHostAccess(proton, 'chrome://settings'), false);
});

test('proxy auth requires broad HTTP and HTTPS host access plus both auth permissions', () => {
  assert.equal(allowsProxyAuth(proton), true);
  assert.equal(allowsProxyAuth({ ...proton, permissions: ['webRequest'] }), false);
  assert.equal(allowsProxyAuth({ ...proton, hostPermissions: ['https://*/'] }), false);
});
