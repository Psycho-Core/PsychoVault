const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

test('rapid new tabs navigate their own IDs and notices do not replace the VPN warning', async () => {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) elements.set(id, { value: '', textContent: '', children: [], hidden: false,
      attributes: {}, setAttribute(key, value) { this.attributes[key] = value; }, addEventListener() {}, replaceChildren() { this.children = []; }, append(...items) { this.children.push(...items); },
      getBoundingClientRect() { return { x: 44, y: 80, width: 300, height: 500 }; }, blur() {} });
    return elements.get(id);
  }
  const document = { getElementById: element, createElement: () => element(Symbol()), activeElement: null };
  const calls = [];
  const browser = { create: async ({ id }) => calls.push(['create', id]), activate: async () => {}, close: async () => {},
    bounds: async () => {}, navigate: async ({ id }) => calls.push(['navigate', id]), command: async () => {}, addListener: () => {} };
  const window = { Capacitor: { Plugins: { PsychoBrowser: browser } }, addEventListener() {} };
  const context = { window, document, setTimeout: () => 1, clearTimeout: () => {}, ResizeObserver: class { observe() {} }, PsychoUrl: { destination: x => x } };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../www/app.js'), 'utf8'), context);
  await new Promise(resolve => setImmediate(resolve));
  calls.length = 0;
  element('new-tab').onclick(); element('new-tab').onclick();
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(calls.filter(x => x[0] === 'navigate').map(x => x[1]).sort(), ['2', '3']);
  const warning = element('vpn-warning'); warning.textContent = 'Traffic is not protected by Proton.';
  element('blocker').onclick();
  assert.equal(warning.textContent, 'Traffic is not protected by Proton.');
  assert.match(element('notice').textContent, /ad blocker/);
});

test('sidebar opens on request and closes on timeout or website touch', async () => {
  const elements = new Map(), listeners = new Map(), timers = new Map(); let timerId = 0;
  function element(id) {
    if (!elements.has(id)) elements.set(id, { hidden: id === 'rail', value: '', textContent: '', attributes: {}, handlers: {},
      setAttribute(k, v) { this.attributes[k] = v; }, addEventListener(k, fn) { this.handlers[k] = fn; },
      replaceChildren() {}, append() {}, getBoundingClientRect() { return { x: 0, y: 80, width: 320, height: 500 }; }, blur() {} });
    return elements.get(id);
  }
  const document = { getElementById: element, createElement: () => element(Symbol()), activeElement: null };
  const browser = { create: async () => {}, activate: async () => {}, close: async () => {}, bounds: async () => {},
    navigate: async () => {}, command: async () => {}, addListener: (name, fn) => listeners.set(name, fn) };
  const context = { document, window: { Capacitor: { Plugins: { PsychoBrowser: browser } }, addEventListener() {} },
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
