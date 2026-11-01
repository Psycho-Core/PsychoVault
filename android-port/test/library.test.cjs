const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../core/library.js');
function disk(initial = null) {
  let data = initial, fail = false;
  return {read: async () => structuredClone(data), write: async v => {if (fail) throw Error('disk full'); data = structuredClone(v)}, get data(){return data}, set fail(v){fail=v}};
}
test('bookmarks persist and concurrent writes are serialized without losing updates', async () => {
  const d = disk(), lib = create(d,{now:()=>42});
  await Promise.all([lib.addBookmark({url:'https://user:secret@example.com/a',title:'One'}), lib.addBookmark({url:'https://example.org/',title:'Two'})]);
  assert.deepEqual((await create(d).listBookmarks()).map(x=>x.url),['https://example.org/','https://example.com/a']);
  assert.equal(await lib.isBookmarked('https://example.com/a'),true);
  await lib.addBookmark({url:'https://example.com/a'});
  assert.equal((await lib.listBookmarks()).length,2);
  await lib.removeBookmark('https://example.com/a'); assert.equal((await lib.listBookmarks()).length,1);
});
test('private and non-remembered navigations never reach disk; history is deduplicated', async () => {
  const d=disk(), lib=create(d,{now:()=>5});
  assert.equal(await lib.recordVisit({url:'https://private.test/'},{profile:'private',remember:true}),false);
  assert.equal(await lib.recordVisit({url:'https://private.test/'},{profile:'regular',remember:false}),false);
  assert.equal(await lib.recordVisit({url:'https://private.test/'}),false); assert.equal(d.data,null);
  await lib.recordVisit({url:'https://example.com',title:'old'},{profile:'regular',remember:true});
  await lib.recordVisit({url:'https://example.com',title:'new'},{profile:'regular',remember:true});
  assert.equal((await lib.listHistory()).length,1);assert.equal((await lib.listHistory())[0].title,'new');
  assert.equal((await lib.search('NEW'))[0].type,'history');
  await lib.clearHistory(); assert.deepEqual(await create(d).listHistory(),[]);
});
test('write failure leaves previous state intact and later actions recover', async () => {
  const d=disk(), lib=create(d); await lib.addBookmark({url:'https://one.test'}); d.fail=true;
  await assert.rejects(lib.addBookmark({url:'https://two.test'}),/disk full/);
  assert.equal((await lib.listBookmarks()).length,1); d.fail=false;
  await lib.addBookmark({url:'https://three.test'}); assert.equal(d.data.bookmarks.length,2);
});
test('bad stored versions and unsafe schemes fail without overwriting data', async () => {
  const d=disk({version:2,bookmarks:[],history:[]}), lib=create(d);
  await assert.rejects(lib.addBookmark({url:'https://example.com'}),/Unsupported or corrupt/); assert.equal(d.data.version,2);
  const good=disk(), lib2=create(good); await assert.rejects(lib2.addBookmark({url:'javascript:alert(1)'}),/Only HTTP/); assert.equal(good.data,null);
});
test('desktop and internal bridge contracts route library operations while native calls pass through', async () => {
  const d=disk(), lib=create(d), calls=[];
  const native={invoke:(channel,args)=>{calls.push([channel,args]);return 'native'},subscribe:()=>()=>{}};
  const transport=require('../core/library-transport.js').create(native,lib);
  const chrome=require('../ui/desktop-bridge.js').create(transport);
  const page=require('../ui/internal-page-bridge.js').create(transport);
  await chrome.bookmarks.add({url:'https://one.test',title:'One'});
  assert.equal((await chrome.bookmarks.list())[0].title,'One');
  await lib.recordVisit({url:'https://two.test',title:'Two'},{profile:'regular',remember:true});
  await page.history.clear();assert.deepEqual(await chrome.history.list(),[]);
  assert.equal(await chrome.nav.back(),'native');assert.deepEqual(calls,[['nav:back',[]]]);
});
test('atomic storage update serializes two library owners without losing writes', async () => {
  let data = null;
  const storage = {
    read: async () => structuredClone(data),
    write: async v => { data = structuredClone(v); },
    update: async fn => { data = fn(structuredClone(data)); return structuredClone(data); }
  };
  const a = create(storage, {now: () => 1}), b = create(storage, {now: () => 2});
  await Promise.all([a.addBookmark({url: 'https://a.test'}), b.addBookmark({url: 'https://b.test'})]);
  assert.equal((await a.listBookmarks()).length, 2);
  assert.equal((await b.listBookmarks()).length, 2);
});
test('settings page generation disables unsupported controls and keeps CSP-safe scripts', () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), {spawnSync} = require('node:child_process');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-settings-'));
  try {
    const dest = path.join(dir, 'ui');
    const result = spawnSync(process.execPath, [path.join(__dirname, '../scripts/prepare-ui.cjs'), dest], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    const html = fs.readFileSync(path.join(dest, 'settings.html'), 'utf8');
    assert.doesNotMatch(html, /<script>/);
    assert.match(html, /settings\.js/);
    const js = fs.readFileSync(path.join(dest, 'settings.js'), 'utf8');
    assert.match(js, /Not connected on Android yet/);
    assert.match(js, /SUPPORTED/);
    const chrome = fs.readFileSync(path.join(dest, 'index.html'), 'utf8');
    assert.match(chrome, /library\.js/);
    assert.match(fs.readFileSync(path.join(dest, 'chrome-bootstrap.js'), 'utf8'), /library:visit/);
  } finally { fs.rmSync(dir, {recursive: true, force: true}); }
});
