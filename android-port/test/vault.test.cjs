const { test } = require('node:test');
const assert = require('node:assert/strict');
const { open } = require('../www/vault');
function memory() { let value = null, fail = false; return { getItem: () => value, setItem: (_, v) => { if (fail) throw Error('write failed'); value = v; }, raw: () => value, setRaw: v => { value = v; }, fail: () => { fail = true; } }; }
const master = 'synthetic test master password';
const fake = { site:'https://example.test/login', user:'fake-user', pass:'fake-secret', note:'fake-note' };
test('vault stores only ciphertext, locks, unlocks and removes entries', async () => {
  const storage = memory(), vault = open(storage);
  assert.equal(vault.exists(), false); await vault.create(master); await vault.add(fake);
  assert.equal(vault.list()[0].site, 'example.test');
  assert.ok(!storage.raw().includes('fake-secret'));
  const id = vault.list()[0].id;
  vault.lock(); assert.throws(() => vault.list(), /locked/);
  await assert.rejects(vault.unlock('wrong synthetic master password'));
  assert.equal(vault.unlocked(), false);
  await vault.unlock(master); assert.equal(vault.list()[0].pass, 'fake-secret');
  await vault.remove(id); vault.lock(); await vault.unlock(master);
  assert.deepEqual(vault.list(), []);
});
test('vault does not overwrite corrupt data or mutate entries after a write failure', async () => {
  const storage = memory(); storage.setRaw('{broken'); const corrupt = open(storage);
  await assert.rejects(corrupt.create(master), /already exists/);
  await assert.rejects(corrupt.unlock(master)); assert.equal(storage.raw(), '{broken');
  const other = memory(), vault = open(other); await vault.create(master); other.fail();
  await assert.rejects(vault.add(fake), /write failed/); assert.deepEqual(vault.list(), []);
});
test('locking during derivation prevents a pending vault operation from writing', async () => {
  const storage = memory(), vault = open(storage);
  const pending = vault.create(master); vault.lock();
  await assert.rejects(pending, /locked/); assert.equal(storage.raw(), null);
});
test('concurrent external changes are not overwritten', async () => {
  const storage = memory(), vault = open(storage); await vault.create(master);
  storage.setRaw('changed');
  await assert.rejects(vault.add(fake), /changed outside/);
  assert.equal(storage.raw(), 'changed'); assert.deepEqual(vault.list(), []);
});
