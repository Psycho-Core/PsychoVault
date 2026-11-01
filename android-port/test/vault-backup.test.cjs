const { test } = require('node:test');
const assert = require('node:assert/strict');
const android = require('../www/vault-backup');
const desktop = require('../../app/vault-transfer');
const password = 'synthetic test password';
const entries = [{id:'fake-1', site:'example.test', user:'test-ü', pass:'not-a-real-secret', note:'synthetic'}];

test('Android codec decrypts a desktop encrypted backup', async () => {
  const encrypted = desktop.encryptBackup(entries, password);
  assert.deepEqual(await android.decrypt(encrypted, password), entries);
});
test('desktop codec decrypts Android encrypted backups', async () => {
  const encrypted = await android.encrypt(entries, password);
  assert.deepEqual(desktop.decryptBackup(encrypted, password), entries);
  assert.ok(!encrypted.includes('not-a-real-secret'));
});
test('wrong passwords and tampering do not decrypt', async () => {
  const encrypted = await android.encrypt(entries, password);
  await assert.rejects(android.decrypt(encrypted, 'wrong synthetic password'));
  const changed = JSON.parse(encrypted);
  const bytes = Buffer.from(changed.data, 'base64'); bytes[0] ^= 1; changed.data = bytes.toString('base64');
  await assert.rejects(android.decrypt(JSON.stringify(changed), password));
});
test('invalid formats, malformed encodings and weak export passwords are rejected', async () => {
  await assert.rejects(android.encrypt([], 'short'), /at least 12/);
  await assert.rejects(android.decrypt('{"format":"wrong"}', password), /Unsupported/);
  const invalid = { format:'psychovault-backup', version:1, kdf:'scrypt-16384-8-1', salt:'invalid!', iv:'', tag:'', data:'' };
  await assert.rejects(android.decrypt(JSON.stringify(invalid), password), /Invalid/);
  await assert.rejects(android.decrypt('x'.repeat(21 * 1024 * 1024), password), /too large/);
});
