const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const source = path.join(root, 'engine/titanium');
const metadata = require('../engine/UPSTREAM.json');
const prepare = path.join(root, 'scripts/prepare-engine.sh');

test('pinned upstream metadata identifies Chromium engine inputs', () => {
  assert.equal(metadata.commit, '5e9070a98b48f6b6cb239bf90b8718b91795b7c5');
  assert.equal(metadata.submodules.vanadium.commit, '0202b7badff99434822ba5e32e0d738792868455');
  assert.match(fs.readFileSync(path.join(source, 'args.gn'), 'utf8'), /is_desktop_android = true/);
  assert.match(fs.readFileSync(path.join(source, 'patch.sh'), 'utf8'), /ExtensionActionPopup/);
  assert.match(fs.readFileSync(path.join(source, 'LICENSE'), 'utf8'), /GNU GENERAL PUBLIC LICENSE/);
});
test('vendored Titanium source matches the recorded checksums', () => {
  const checks = fs.readFileSync(path.join(root, 'engine/SHA256SUMS'), 'utf8').trim().split('\n');
  assert.ok(checks.length >= 10);
  for (const line of checks) {
    const match = /^([a-f0-9]{64})  \.\/(.+)$/.exec(line); assert.ok(match, line);
    assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(source,match[2]))).digest('hex'), match[1], match[2]);
  }
});
test('preparation refuses to overwrite an existing workspace', () => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(),'pv-engine-test-'));
  try {
    const dir = path.join(temp,'existing'); fs.mkdirSync(dir); fs.writeFileSync(path.join(dir,'keep'),'important');
    const result = spawnSync('bash',[prepare,dir],{encoding:'utf8'});
    assert.notEqual(result.status,0); assert.match(result.stderr,/not overwriting/); assert.equal(fs.readFileSync(path.join(dir,'keep'),'utf8'),'important');
  } finally { fs.rmSync(temp,{recursive:true,force:true}); }
});
test('preparation rejects relative paths and repository-local workspaces', () => {
  const relative = spawnSync('bash',[prepare,'relative/path'],{encoding:'utf8'});
  assert.notEqual(relative.status,0); assert.match(relative.stderr,/absolute Linux path/);
  const local = spawnSync('bash',[prepare,path.join(root,'should-not-be-created')],{encoding:'utf8'});
  assert.notEqual(local.status,0); assert.match(local.stderr,/outside the PsychoVault repository/);
});
test('Windows entry point refuses to build until the new port is ready and records notes', () => {
  const batch = fs.readFileSync(path.join(root,'BUILD-ANDROID.bat'),'utf8');
  assert.match(batch,/Status: not built/); assert.match(batch,/Release-Notes-Android\.txt/); assert.match(batch,/exit \/b 1/);
  assert.equal((batch.match(/\n/g)||[]).length,(batch.match(/\r\n/g)||[]).length);
});
