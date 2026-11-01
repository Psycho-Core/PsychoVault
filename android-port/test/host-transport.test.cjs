const {test} = require('node:test');
const assert = require('node:assert/strict');
const resolver = require('../ui/navigation-resolver');
const {create} = require('../ui/native-transport');
const {measure} = require('../ui/host-layout');

function harness(options = {}) {
  const calls = [], timers = new Map(); let nextTimer = 1;
  const transport = create({native:{post:json => calls.push(JSON.parse(json))}, resolver,
    setTimeout:fn => { const id = nextTimer++; timers.set(id, fn); return id; }, clearTimeout:id => timers.delete(id), ...options});
  return {transport, calls, timers};
}
test('address resolver distinguishes URLs, searches and profile-specific internal pages', () => {
  assert.equal(resolver.resolve('example.com/a'), 'https://example.com/a');
  assert.equal(resolver.resolve('http://localhost:8080'), 'http://localhost:8080/');
  assert.equal(resolver.resolve('hello world'), 'https://duckduckgo.com/?q=hello%20world');
  assert.equal(resolver.resolve('', {}, true), resolver.PAGE_ORIGIN + '/newtab.html?private=1');
  assert.equal(resolver.resolve('psychovault://settings', {}, true), resolver.PAGE_ORIGIN + '/settings.html?private=1');
  assert.equal(resolver.display(resolver.PAGE_ORIGIN + '/settings.html?private=0'), 'psychovault://settings');
  for (const input of ['javascript:alert(1)', 'file:///data', 'chrome://settings', 'data:text/html,x', 'psychovault://recovery']) {
    assert.throws(() => resolver.resolve(input));
  }
  assert.equal(resolver.resolve('custom query', {search:'custom:x', customSearchEngines:[{shortcut:'x',url:'https://search.example/?q=%s'}]}), 'https://search.example/?q=custom%20query');
  assert.throws(() => resolver.resolve('x', {search:'custom:x',customSearchEngines:[{shortcut:'x',url:'http://search.example/?q=%s'}]}));
  assert.throws(() => resolver.resolve('x'.repeat(8193)));
});
test('transport correlates replies, ignores duplicates and rejects timeouts', async () => {
  const {transport, calls, timers} = harness();
  const ready = transport.start();
  assert.equal(calls[0].channel, 'ui:ready');
  transport.reply(0, true, JSON.stringify({tabs:[], settings:{search:'duckduckgo'}, private:false}));
  await ready;
  const pending = transport.invoke('nav:go', ['example.com']);
  assert.equal(calls[1].args[0], 'https://example.com/');
  transport.reply(calls[1].id, true, 'true');
  assert.equal(await pending, true);
  transport.reply(calls[1].id, false, '"late error"');
  const timeout = transport.invoke('nav:reload');
  const rejected = assert.rejects(timeout, /timed out/);
  timers.get([...timers.keys()].at(-1))();
  await rejected;
  transport.dispose();
  await assert.rejects(transport.invoke('nav:back'), /closed/);
});
test('transport bounds requests and never forwards rejected privileged schemes', async () => {
  const {transport, calls} = harness();
  const ready = transport.start(); transport.reply(0, true, '{}'); await ready;
  await assert.rejects(transport.invoke('nav:go', ['chrome://flags']), /not available/);
  assert.equal(calls.length, 1);
  const requests = Array.from({length:64}, () => transport.invoke('nav:reload').catch(() => {}));
  await assert.rejects(transport.invoke('nav:reload'), /Too many/);
  transport.dispose();
  await Promise.all(requests);
});
test('state display hides only the exact profile-matched internal URL', () => {
  const {transport} = harness(); const events = [];
  transport.subscribe('state', value => events.push(value));
  transport.emit('state', {private:true, url:resolver.PAGE_ORIGIN+'/newtab.html?private=1', tabs:[{url:'https://example.com/'}], settings:{}});
  assert.equal(events[0].url, 'psychovault://newtab');
  assert.equal(events[0].tabs[0].url, 'https://example.com/');
  transport.emit('state', {private:false, url:resolver.PAGE_ORIGIN+'/newtab.html?private=1', settings:{}});
  assert.match(events[1].url, /^chrome:/);
  transport.dispose();
});
test('layout reports chrome hit regions and panel inset without claiming the page rectangle', () => {
  const elements = new Map();
  function el(id, hidden = false, width = 100, height = 40) {
    const value = {id, hidden, classList:{contains:() => false}, style:{}, getBoundingClientRect:() => ({left:0,top:0,right:width,bottom:height,width,height})};
    elements.set(id, value); return value;
  }
  el('top'); el('panel', false, 240); el('menu', true);
  const doc = {getElementById:id => elements.get(id) || null};
  const globalDocument = global.document, globalStyle = global.getComputedStyle; global.document = doc; global.getComputedStyle = () => ({display:'block'});
  try {
    const layout = measure(doc, 160, 2);
    assert.equal(layout.chromeTop, 80); assert.equal(layout.extraTop, 0); assert.equal(layout.panelLeft, 240);
    assert.equal(layout.hitRegions.length, 2);
    assert.ok(!layout.hitRegions.some(r => r.left === 240));
    elements.get('top').getBoundingClientRect = () => ({height:100});
    assert.equal(measure(doc, 160, 2).extraTop, 20);
  } finally { if (globalDocument === undefined) delete global.document; else global.document = globalDocument; if (globalStyle === undefined) delete global.getComputedStyle; else global.getComputedStyle = globalStyle; }
});
