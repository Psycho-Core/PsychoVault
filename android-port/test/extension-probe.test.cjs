const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { inspect } = require('../extension-probe/probe');
const manifest = require('../extension-probe/manifest.json');
const proton = require('../../app/builtin/proton-vpn/manifest.json');
test('probe requests Proton extension permissions without host access beyond HTTP and HTTPS', () => {
  for (const permission of proton.permissions) assert.ok(manifest.permissions.includes(permission), permission);
  assert.deepEqual(manifest.host_permissions, ['http://*/*', 'https://*/*']);
});
test('probe reports only capabilities and control level, not configured proxy values', async () => {
  const out = await inspect({runtime:{sendMessage() {}}, proxy:{settings:{get(_details, callback) { callback({ levelOfControl:'controllable_by_this_extension', value:{server:'secret.invalid'} }); }}}});
  assert.match(out, /proxy settings: available/);
  assert.match(out, /proxy control level: controllable_by_this_extension/);
  assert.match(out, /tabs: missing/);
  assert.doesNotMatch(out, /secret/);
});
test('probe handles blocked proxy reads and never calls a setter', async () => {
  let writes = 0;
  const output = await inspect({runtime:{lastError:{message:'private info'}}, proxy:{settings:{get(_, cb) { cb(null); }, set() { writes++; }}}});
  assert.match(output, /proxy control level: unavailable/);
  assert.doesNotMatch(output, /private info/);
  assert.equal(writes, 0);
});
test('probe source has no network or dynamic script execution calls', () => {
  for (const file of ['probe.js', 'popup.js']) {
    const content = fs.readFileSync(path.join(__dirname, '../extension-probe', file), 'utf8');
    assert.doesNotMatch(content, /\b(fetch|XMLHttpRequest|WebSocket|eval)\s*\(/);
  }
});
