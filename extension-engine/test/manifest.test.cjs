'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { parseManifest, safeResourcePath, matchPattern, MAX_MANIFEST_BYTES } = require('../manifest');

const root = path.resolve(__dirname, '../..');
const proton = fs.readFileSync(path.join(root, 'app/builtin/proton-vpn/manifest.json'), 'utf8');
const dark = fs.readFileSync(path.join(root, 'app/builtin/dark-reader/manifest.json'), 'utf8');
const minimal = JSON.stringify({ manifest_version: 3, name: 'Test', version: '1.0' });

test('reads the bundled Proton manifest without granting anything', () => {
  const parsed = parseManifest(proton);
  assert.equal(parsed.manifest.name, 'Proton VPN: Fast & Secure');
  assert.equal(parsed.requirements.worker, 'js/service.js');
  assert.equal(parsed.requirements.popup, 'popup.html');
  assert.ok(parsed.requirements.permissions.includes('proxy'));
  assert.ok(parsed.requirements.hostPermissions.includes('https://*/'));
  assert.deepEqual(parsed.requirements.unknownPermissions, []);
  assert.throws(() => { parsed.manifest.name = 'changed'; }, TypeError);
});

test('reads the bundled Dark Reader manifest', () => {
  const parsed = parseManifest(dark);
  assert.equal(parsed.requirements.worker, 'background/index.js');
  assert.ok(parsed.requirements.permissions.includes('fontSettings'));
  assert.deepEqual(parsed.requirements.unknownPermissions, []);
});

test('rejects duplicate and escaped duplicate keys', () => {
  assert.throws(() => parseManifest('{"manifest_version":3,"name":"a","name":"b","version":"1"}'), /duplicate manifest key/);
  assert.throws(() => parseManifest('{"manifest_version":3,"name":"a","\\u006eame":"b","version":"1"}'), /duplicate manifest key/);
});

test('rejects MV2, malformed versions and oversized manifests', () => {
  assert.throws(() => parseManifest(JSON.stringify({ manifest_version: 2, name: 'x', version: '1' })), /Manifest V3/);
  assert.throws(() => parseManifest(JSON.stringify({ manifest_version: 3, name: 'x', version: '01' })), /invalid extension version/);
  assert.throws(() => parseManifest(' '.repeat(MAX_MANIFEST_BYTES + 1)), /1 MiB/);
});

test('rejects unsafe resource paths', () => {
  for (const bad of ['/etc/passwd', '../secret', 'a/../../secret', 'a\\b', 'https://evil/x', 'a?b', 'a#b', 'a/%2e%2e/b', 'a//b']) {
    assert.throws(() => safeResourcePath(bad), /inside the extension package|invalid escaping/);
  }
  assert.equal(safeResourcePath('js/service.js'), 'js/service.js');
});

test('rejects remote or unsafe extension CSP, including default-src fallback', () => {
  const base = { manifest_version: 3, name: 'x', version: '1' };
  for (const csp of [
    "script-src 'self' https://evil.example",
    "script-src 'unsafe-eval'",
    "default-src https://evil.example",
    "default-src 'self'; script-src 'unsafe-inline'",
    "script-src 'self'; script-src 'none'"
  ]) {
    assert.throws(() => parseManifest(JSON.stringify({ ...base, content_security_policy: { extension_pages: csp } })), /CSP/);
  }
});

test('validates match patterns and accepts Proton short host form', () => {
  assert.equal(matchPattern('https://*/'), 'https://*/');
  assert.equal(matchPattern('*://*/*'), '*://*/*');
  assert.throws(() => matchPattern('javascript:alert(1)'), /invalid match pattern/);
  assert.throws(() => matchPattern('https://*example.com/*'), /invalid match-pattern host/);
});
