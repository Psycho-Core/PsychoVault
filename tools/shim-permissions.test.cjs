'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const main = fs.readFileSync(path.join(__dirname, '../app/main.js'), 'utf8');

function loadHelpers(records, manifests) {
  const start = main.indexOf('function shimManifestForId(');
  const end = main.indexOf('// The extension id a message really came from');
  const code = main.slice(start, end);
  const sandbox = {
    extRecords: () => records,
    extReadManifest: (dir) => {
      if (!Object.hasOwn(manifests, dir)) throw new Error('missing');
      return manifests[dir];
    }
  };
  vm.createContext(sandbox);
  vm.runInContext(code + '\nthis.helpers = { shimManifestForId, shimHasPermission };', sandbox);
  return sandbox.helpers;
}

test('shim permission checks use the installed manifest, not the request', () => {
  const records = [{ id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', dir: '/ext/proton' }];
  const manifests = { '/ext/proton': { permissions: ['proxy', 'privacy', 'tabs'] } };
  const h = loadHelpers(records, manifests);
  assert.equal(h.shimHasPermission('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 'proxy'), true);
  assert.equal(h.shimHasPermission('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 'storage'), false);
  assert.equal(h.shimHasPermission('bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb', 'proxy'), false);
});

test('proxy, privacy and sensitive tab data are gated in onShimCall', () => {
  assert.match(main, /channel\.startsWith\('extshim:proxy:'\) && !shimHasPermission\(real, 'proxy'\)/);
  assert.match(main, /channel\.startsWith\('extshim:webrtc:'\) && !shimHasPermission\(real, 'privacy'\)/);
  assert.match(main, /if \(!hasTabs\) list = list\.map/);
  assert.match(main, /another extension is controlling the proxy/);
  assert.match(main, /previous proxy state restored/);
});
