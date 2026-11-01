const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const PsychoStore = require('../www/store.js');

test('rapid new tabs navigate their own IDs and notices do not replace the VPN warning', async () => {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) elements.set(id, { value: '', textContent: '', children: [], hidden: false,
      attributes: {}, setAttribute(key, value) { this.attributes[key] = value; }, addEventListener() {}, replaceChildren() { this.children = []; }, append(...items) { this.children.push(...items); },
      getBoundingClientRect() { return { x: 44, y: 80, width: 300, height: 500 }; }, blur() {} });
    return elements.get(id);
  }
  const document = { body: { dataset: {} }, getElementById: element, createElement: () => element(Symbol()), createTextNode: text => ({textContent:text}), activeElement: null };
  const calls = [];
  const browser = { create: async ({ id }) => calls.push(['create', id]), activate: async () => {}, close: async () => {},
    configureBlocker: async () => {}, bounds: async () => {}, navigate: async ({ id }) => calls.push(['navigate', id]), command: async () => {}, addListener: () => {} };
  const window = { Capacitor: { Plugins: { PsychoBrowser: browser } }, addEventListener() {} };
  const context = { window, document, PsychoStore, setTimeout: () => 1, clearTimeout: () => {}, ResizeObserver: class { observe() {} }, PsychoUrl: { destination: x => x } };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../www/app.js'), 'utf8'), context);
  await new Promise(resolve => setImmediate(resolve));
  calls.length = 0;
  element('new-tab').onclick(); element('new-tab').onclick();
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(calls.filter(x => x[0] === 'navigate').map(x => x[1]).sort(), ['2', '3']);
  const warning = element('vpn-warning'); warning.textContent = 'Traffic is not protected by Proton.';
  element('blocker').onclick();
  assert.equal(warning.textContent, 'Traffic is not protected by Proton.');
  assert.ok(element('panel-content').children.some(child => child.textContent === 'Ad blocker'));
});

test('sidebar opens on request and closes on timeout or website touch', async () => {
  const elements = new Map(), listeners = new Map(), timers = new Map(); let timerId = 0;
  function element(id) {
    if (!elements.has(id)) elements.set(id, { hidden: id === 'rail', value: '', textContent: '', attributes: {}, handlers: {},
      setAttribute(k, v) { this.attributes[k] = v; }, addEventListener(k, fn) { this.handlers[k] = fn; },
      replaceChildren() {}, append() {}, getBoundingClientRect() { return { x: 0, y: 80, width: 320, height: 500 }; }, blur() {} });
    return elements.get(id);
  }
  const document = { body: { dataset: {} }, getElementById: element, createElement: () => element(Symbol()), createTextNode: text => ({textContent:text}), activeElement: null };
  const browser = { create: async () => {}, activate: async () => {}, close: async () => {}, configureBlocker: async () => {}, bounds: async () => {},
    navigate: async () => {}, command: async () => {}, addListener: (name, fn) => listeners.set(name, fn) };
  const context = { document, PsychoStore, window: { Capacitor: { Plugins: { PsychoBrowser: browser } }, addEventListener() {} },
    setTimeout: (fn, ms) => { assert.equal(ms, 5000); const id = ++timerId; timers.set(id, fn); return id; },
    clearTimeout: id => timers.delete(id), ResizeObserver: class { observe() {} }, PsychoUrl: { destination: x => x } };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../www/app.js'), 'utf8'), context);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(element('rail').hidden, true);
  element('rail-toggle').onclick();
  assert.equal(element('rail').hidden, false);
  assert.equal(element('rail-toggle').attributes['aria-expanded'], 'true');
  assert.equal(timers.size, 1);
  [...timers.values()][0]();
  assert.equal(element('rail').hidden, true);
  assert.equal(element('rail-toggle').attributes['aria-expanded'], 'false');
  element('rail-toggle').onclick();
  listeners.get('pageTouched')();
  assert.equal(element('rail').hidden, true);
  assert.equal(timers.size, 0);
});

test('page events, bookmarks and menu use persistent storage', async () => {
  const nodes = new Map(), listeners = new Map(); let raw = null;
  function element(id) {
    if (!nodes.has(id)) nodes.set(id, { hidden: id === 'rail', value: '', textContent: '', children: [], dataset: {},
      setAttribute() {}, addEventListener() {}, replaceChildren() { this.children = []; },
      append(...children) { this.children.push(...children); },
      getBoundingClientRect() { return { x: 0, y: 80, width: 320, height: 500 }; }, blur() {} });
    return nodes.get(id);
  }
  const localStorage = { getItem: () => raw, setItem: (_, value) => { raw = value; } };
  const browser = { create: async () => {}, activate: async () => {}, close: async () => {}, configureBlocker: async () => {}, bounds: async () => {},
    navigate: async () => {}, command: async () => {}, addListener: (name, cb) => listeners.set(name, cb) };
  const document = { body: {dataset:{}}, getElementById: element, createElement: () => element(Symbol()), createTextNode: text => ({textContent:text}), activeElement: null };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../www/app.js'), 'utf8'), {
    document, PsychoStore, window: { Capacitor: {Plugins: {PsychoBrowser: browser}}, localStorage, addEventListener() {} },
    setTimeout: () => 1, clearTimeout() {}, ResizeObserver: class { observe() {} }, PsychoUrl: {destination: x => x}
  });
  await new Promise(resolve => setImmediate(resolve));
  listeners.get('pageChanged')({id:'1', url:'https://example.com/', title:'Example'});
  element('bookmark').onclick();
  assert.equal(PsychoStore.open(localStorage).read().bookmarks[0].url, 'https://example.com/');
  assert.equal(PsychoStore.open(localStorage).read().history[0].title, 'Example');
  element('menu').onclick();
  assert.equal(element('panel').hidden, false);
  assert.ok(element('panel-content').children.some(child => child.textContent === 'Bookmarks'));
  element('panel-close').onclick(); assert.equal(element('panel').hidden, true);
});

test('saved session reopens its tabs and selects the previously active tab', async () => {
  const elements = new Map(), calls = [], listeners = new Map(); let raw = null;
  const storage = { getItem: () => raw, setItem: (_, v) => { raw = v; } };
  const db = PsychoStore.open(storage);
  db.settings({restoreSession: true});
  db.saveSession({tabs: ['https://one.example/', 'https://two.example/'], active: 0});
  function element(id) {
    if (!elements.has(id)) elements.set(id, { value:'', textContent:'', hidden:false, children:[],
      setAttribute() {}, addEventListener() {}, replaceChildren() { this.children = []; }, append(...children) { this.children.push(...children); },
      getBoundingClientRect() { return {x:0,y:0,width:320,height:400}; }, blur() {} });
    return elements.get(id);
  }
  const browser = { create: async ({id}) => calls.push(['create', id]), activate: async ({id}) => calls.push(['activate', id]),
    navigate: async ({id, url}) => calls.push(['navigate', id, url]), configureBlocker: async () => {}, bounds: async () => {}, close: async () => {}, command: async () => {}, addListener: (event, fn) => listeners.set(event, fn) };
  const document = { body: {dataset:{}}, getElementById:element, createElement:() => element(Symbol()), createTextNode: text => ({textContent:text}), activeElement:null };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../www/app.js'), 'utf8'), { document, PsychoStore,
    window: {Capacitor:{Plugins:{PsychoBrowser:browser}}, localStorage:storage, addEventListener() {}},
    setTimeout:()=>1, clearTimeout(){}, ResizeObserver:class {observe() {}}, PsychoUrl:{destination:x=>x} });
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(calls.filter(c => c[0] === 'navigate'), [['navigate','1','https://one.example/'], ['navigate','2','https://two.example/']]);
  assert.deepEqual(calls.filter(c => c[0] === 'activate'), [['activate','1']]);
  assert.deepEqual(PsychoStore.open(storage).read().session, {tabs:['https://one.example/', 'https://two.example/'], active:0});
  listeners.get('pageChanged')({id:'2',url:'https://changed.example/',title:'Changed'});
  assert.deepEqual(PsychoStore.open(storage).read().session.tabs, ['https://one.example/', 'https://changed.example/']);
});
