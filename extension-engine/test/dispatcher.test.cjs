'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { ExtensionEngine } = require('../dispatcher');

const protonManifest = fs.readFileSync(path.resolve(__dirname, '../../app/builtin/proton-vpn/manifest.json'), 'utf8');
const id = 'jplgfhpmjnbigmhklmmbgecoobifkmpa';
const other = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
const minimal = JSON.stringify({ manifest_version: 3, name: 'Minimal', version: '1.0', permissions: ['storage'] });

function setup(options) {
  const engine = new ExtensionEngine(options);
  engine.registerExtension({ profileId: 'default', extensionId: id, manifest: protonManifest });
  return engine;
}
function token(engine, overrides = {}) {
  return engine.createToken({ profileId: 'default', extensionId: id, kind: 'worker', origin: `chrome-extension://${id}`, ...overrides });
}
function req(method, params = [], requestId = 'r1') { return { id: requestId, method, params }; }

test('dispatches only through an opaque trusted token and keeps profiles separate', async () => {
  const engine = setup();
  engine.registerExtension({ profileId: 'private', extensionId: id, manifest: protonManifest });
  const seen = [];
  engine.registerHandler('storage.local.get', (context, params) => { seen.push([context.profileId, context.extensionId]); return params; });
  const a = token(engine);
  const b = token(engine, { profileId: 'private' });
  assert.equal((await engine.dispatch(a, req('storage.local.get', ['key']))).ok, true);
  assert.equal((await engine.dispatch(b, req('storage.local.get', ['key']))).ok, true);
  assert.deepEqual(seen, [['default', id], ['private', id]]);
  assert.equal((await engine.dispatch({}, req('storage.local.get'))).error.code, 'invalid-context');
  assert.throws(() => engine.createToken({ profileId: 'default', extensionId: id, kind: 'worker', origin: `chrome-extension://${other}` }), /origin/);
});

test('enforces permissions, context, disable and removal at call time', async () => {
  const engine = setup();
  engine.registerExtension({ profileId: 'default', extensionId: other, manifest: minimal });
  engine.registerHandler('proxy.settings.set', () => ({ ok: true }));
  const noProxy = engine.createToken({ profileId: 'default', extensionId: other, kind: 'worker', origin: `chrome-extension://${other}` });
  assert.equal((await engine.dispatch(noProxy, req('proxy.settings.set'))).error.code, 'permission-denied');
  const content = token(engine, { kind: 'content', origin: 'https://example.com/' });
  assert.equal((await engine.dispatch(content, req('proxy.settings.set'))).error.code, 'context-denied');
  const worker = token(engine);
  engine.setEnabled('default', id, false);
  assert.equal((await engine.dispatch(worker, req('proxy.settings.set'))).error.code, 'disabled');
  engine.setEnabled('default', id, true);
  engine.removeExtension('default', id);
  assert.equal((await engine.dispatch(worker, req('proxy.settings.set'))).error.code, 'not-found');
});

test('scripting needs a trusted tab URL and matching host permission', async () => {
  const engine = setup({ resolveTabUrl: async (_profile, tabId) => tabId === 1 ? 'https://example.com/' : 'chrome://settings' });
  engine.registerHandler('scripting.executeScript', () => []);
  const worker = token(engine);
  assert.equal((await engine.dispatch(worker, req('scripting.executeScript', [{ target: { tabId: 1 } }]))).ok, true);
  assert.equal((await engine.dispatch(worker, req('scripting.executeScript', [{ target: { tabId: 2 } }]))).error.code, 'host-denied');
  const noResolver = setup();
  noResolver.registerHandler('scripting.executeScript', () => []);
  assert.equal((await noResolver.dispatch(token(noResolver), req('scripting.executeScript', [{ target: { tabId: 1 } }]))).error.code, 'host-denied');
});

test('limits request size and in-flight calls without leaking internal errors', async () => {
  const engine = setup({ maxRequestBytes: 32, maxInflight: 1, timeoutMs: 1000 });
  const worker = token(engine);
  assert.equal((await engine.dispatch(worker, req('runtime.getURL', ['x'.repeat(100)]))).error.code, 'invalid-params');
  let release;
  engine.registerHandler('storage.local.get', () => new Promise(resolve => { release = resolve; }));
  const first = engine.dispatch(worker, req('storage.local.get', ['a'], 'first'));
  await new Promise(resolve => setImmediate(resolve));
  const second = await engine.dispatch(worker, req('storage.local.get', ['b'], 'second'));
  assert.equal(second.error.code, 'busy');
  release('done');
  assert.equal((await first).ok, true);
  engine.registerHandler('storage.local.remove', () => { throw new Error('secret path /home/user/token'); });
  const failed = await engine.dispatch(worker, req('storage.local.remove'));
  assert.equal(failed.error.code, 'internal');
  assert.doesNotMatch(JSON.stringify(failed), /secret|token|home/);
});

test('times out a stuck handler', async () => {
  const engine = setup({ timeoutMs: 5 });
  engine.registerHandler('storage.local.set', () => new Promise(() => {}));
  assert.equal((await engine.dispatch(token(engine), req('storage.local.set'))).error.code, 'timeout');
});

test('runtime.getURL cannot escape the package and unknown methods stay unimplemented', async () => {
  const engine = setup();
  const worker = token(engine);
  assert.equal((await engine.dispatch(worker, req('runtime.getURL', ['popup.html']))).result, `chrome-extension://${id}/popup.html`);
  assert.equal((await engine.dispatch(worker, req('runtime.getURL', ['../secret']))).ok, false);
  assert.equal((await engine.dispatch(worker, req('tabs.create'))).error.code, 'unimplemented');
  assert.equal((await engine.dispatch(worker, req('browser.makeCoffee'))).error.code, 'unknown-method');
});

test('a timed-out handler keeps its in-flight slot until it settles and receives an abort signal', async () => {
  const engine = setup({ timeoutMs: 5, maxInflight: 1 });
  let release, aborted = false;
  engine.registerHandler('storage.local.get', (_context, _params, signal) => new Promise(resolve => {
    release = resolve;
    if (signal) {
      aborted = signal.aborted;
      signal.addEventListener('abort', () => { aborted = true; }, { once: true });
    }
  }));
  const worker = token(engine);
  assert.equal((await engine.dispatch(worker, req('storage.local.get', ['a'], 'first'))).error.code, 'timeout');
  assert.equal(aborted, true);
  assert.equal((await engine.dispatch(worker, req('storage.local.get', ['b'], 'second'))).error.code, 'busy');
  release('done');
  await new Promise(resolve => setImmediate(resolve));
  const third = engine.dispatch(worker, req('storage.local.get', ['c'], 'third'));
  await new Promise(resolve => setImmediate(resolve));
  release('done');
  assert.equal((await third).ok, true);
});

test('limits result size and re-checks disable/remove while host access is resolved', async () => {
  const engine = setup({ maxResultBytes: 16, resolveTabUrl: async () => 'https://example.com/' });
  engine.registerHandler('storage.local.get', () => ({ big: 'x'.repeat(100) }));
  assert.equal((await engine.dispatch(token(engine), req('storage.local.get'))).error.code, 'result-too-large');

  let releaseUrl;
  const racing = setup({ resolveTabUrl: () => new Promise(resolve => { releaseUrl = resolve; }) });
  racing.registerHandler('scripting.executeScript', () => []);
  const worker = token(racing);
  const pending = racing.dispatch(worker, req('scripting.executeScript', [{ target: { tabId: 1 } }]));
  await new Promise(resolve => setImmediate(resolve));
  racing.setEnabled('default', id, false);
  releaseUrl('https://example.com/');
  assert.equal((await pending).error.code, 'disabled');
});
