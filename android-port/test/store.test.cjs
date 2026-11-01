const { test } = require('node:test');
const assert = require('node:assert/strict');
const { open, validUrl } = require('../www/store');
function memory(initial = null) { let value = initial; return { getItem: () => value, setItem: (_, v) => { value = v; }, raw: () => value }; }
test('bookmarks, settings and history survive reopening', () => {
  const storage = memory(), db = open(storage);
  db.settings({search:'duckduckgo', theme:'forest'});
  assert.equal(db.toggleBookmark('https://example.com/', 'Example'), true);
  db.visit('https://example.com/', 'Example'); db.visit('https://example.com/', 'New title');
  const restored = open(storage);
  assert.equal(restored.read().settings.search, 'duckduckgo');
  assert.equal(restored.read().settings.theme, 'forest');
  assert.equal(restored.read().history.length, 1);
  assert.equal(restored.read().history[0].title, 'New title');
  assert.equal(restored.read().bookmarks.length, 1);
  assert.equal(restored.toggleBookmark('https://example.com/', 'Example'), false);
  restored.clearHistory(); assert.equal(open(storage).read().history.length, 0);
});
test('corrupt or unavailable data is not overwritten', () => {
  const storage = memory('{broken'); const db = open(storage);
  assert.equal(db.writable(), false); assert.throws(() => db.clearHistory()); assert.equal(storage.raw(), '{broken');
  assert.equal(open({getItem() {throw Error('no');}}).writable(), false);
});
test('failed writes do not mutate in-memory data', () => {
  const db = open({getItem: () => null, setItem() { throw Error('full'); }});
  assert.throws(() => db.settings({theme:'forest'})); assert.equal(db.read().settings.theme, 'navy');
});
test('unsafe addresses and credential-bearing URLs are not persisted', () => {
  assert.equal(validUrl('javascript:alert(1)'), null);
  assert.equal(validUrl('https://user:pass@example.com'), null);
  const db = open(memory()); db.visit('file:///private', ''); assert.equal(db.read().history.length, 0);
  assert.throws(() => db.toggleBookmark('javascript:1', ''));
});

test('individual bookmarks and history entries can be removed without affecting other data', () => {
  const storage = memory(), db = open(storage);
  for (const url of ['https://one.example/', 'https://two.example/']) { db.toggleBookmark(url, url); db.visit(url, url); }
  db.removeBookmark('https://one.example/');
  db.removeHistory('https://two.example/');
  const state = open(storage).read();
  assert.deepEqual(state.bookmarks.map(x => x.url), ['https://two.example/']);
  assert.deepEqual(state.history.map(x => x.url), ['https://one.example/']);
});
test('session restore is opt-in, filters unsafe URLs and is cleared when disabled', () => {
  const storage = memory(), db = open(storage);
  db.saveSession({ tabs: ['https://ignored.example/'], active: 0 });
  assert.deepEqual(db.read().session.tabs, []);
  db.settings({ restoreSession: true });
  db.saveSession({ tabs: ['javascript:alert(1)', 'https://one.example/', 'https://two.example/'], active: 2 });
  const restored = open(storage);
  assert.deepEqual(restored.read().session, { tabs: ['https://one.example/', 'https://two.example/'], active: 1 });
  restored.settings({ restoreSession: false });
  assert.deepEqual(open(storage).read().session, { tabs: [], active: 0 });
  assert.equal(JSON.parse(storage.raw()).session.tabs.length, 0);
});
test('session writes preserve old data if storage fails', () => {
  let fail = false, raw = null;
  const db = open({ getItem: () => raw, setItem: (_, value) => { if (fail) throw Error('full'); raw = value; } });
  db.settings({ restoreSession: true }); db.saveSession({ tabs: ['https://one.example/'], active: 0 });
  fail = true;
  assert.throws(() => db.saveSession({ tabs: ['https://two.example/'], active: 0 }));
  assert.deepEqual(db.read().session.tabs, ['https://one.example/']);
});
test('blocker defaults and custom controls persist without changing other settings', () => {
  const storage = memory(), db = open(storage);
  assert.equal(db.read().settings.blockAds, true);
  assert.equal(db.read().settings.blockUseBundled, true);
  db.settings({blockAds:false, blockUseBundled:false, blockCustomDomains:'ads.example\ntrack.example', blockAllowSites:'news.example'});
  const settings = open(storage).read().settings;
  assert.equal(settings.blockAds, false);
  assert.equal(settings.blockUseBundled, false);
  assert.equal(settings.blockCustomDomains, 'ads.example\ntrack.example');
  assert.equal(settings.blockAllowSites, 'news.example');
  assert.equal(settings.search, 'google');
});
