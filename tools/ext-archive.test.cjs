'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const zlib = require('node:zlib');
const main = fs.readFileSync(path.join(__dirname, '../app/main.js'), 'utf8');
const start = main.indexOf('function extCrxPayload(');
const end = main.indexOf('function extSlug(');
const code = main.slice(start, end);

function load() {
  const writes = [];
  const sandbox = {
    fs: {
      readFileSync: () => { throw new Error('not used'); },
      mkdirSync: () => {},
      writeFileSync: (target, out) => writes.push([target, out])
    },
    path,
    require: (name) => name === 'zlib' ? zlib : require(name),
    writes
  };
  vm.createContext(sandbox);
  vm.runInContext(code + '\nthis.api = { extCrxPayload, extUnzip };', sandbox);
  return sandbox;
}
function central(name, { method = 0, csize = 0, usize = 0, local = 0 } = {}) {
  const b = Buffer.alloc(46 + Buffer.byteLength(name));
  b.writeUInt32LE(0x02014b50, 0);
  b.writeUInt16LE(method, 10);
  b.writeUInt32LE(csize, 20);
  b.writeUInt32LE(usize, 24);
  b.writeUInt16LE(Buffer.byteLength(name), 28);
  b.writeUInt32LE(local, 42);
  b.write(name, 46);
  return b;
}
function local(name, data) {
  const b = Buffer.alloc(30 + Buffer.byteLength(name));
  b.writeUInt32LE(0x04034b50, 0);
  b.writeUInt16LE(Buffer.byteLength(name), 26);
  b.write(name, 30);
  return Buffer.concat([b, data]);
}
function eocd(count, cdOffset) {
  const b = Buffer.alloc(22);
  b.writeUInt32LE(0x06054b50, 0);
  b.writeUInt16LE(count, 8);
  b.writeUInt16LE(count, 10);
  b.writeUInt32LE(cdOffset, 16);
  return b;
}

test('zip reader rejects traversal, truncated records and oversized entries', () => {
  const sandbox = load();
  const name = '../evil.txt';
  const data = Buffer.from('x');
  const loc = local(name, data);
  const cd = central(name, { csize: data.length, usize: data.length, local: 0 });
  const zip = Buffer.concat([loc, cd, eocd(1, loc.length)]);
  assert.throws(() => sandbox.api.extUnzip(zip, '/tmp/ext'), /escapes the destination/);

  const truncated = Buffer.concat([loc, cd.subarray(0, 20), eocd(1, loc.length)]);
  assert.throws(() => sandbox.api.extUnzip(truncated, '/tmp/ext'), /central directory/);

  const big = central('big.bin', { usize: 64 * 1024 * 1024 + 1, csize: 0, local: 0 });
  const bigZip = Buffer.concat([local('big.bin', Buffer.alloc(0)), big, eocd(1, 30 + 7)]);
  assert.throws(() => sandbox.api.extUnzip(bigZip, '/tmp/ext'), /too large/);
});

test('zip reader accepts a stored entry whose size matches', () => {
  const sandbox = load();
  const name = 'manifest.json';
  const data = Buffer.from('{}');
  const loc = local(name, data);
  const cd = central(name, { csize: data.length, usize: data.length, local: 0 });
  const zip = Buffer.concat([loc, cd, eocd(1, loc.length)]);
  assert.equal(sandbox.api.extUnzip(zip, '/tmp/ext'), 1);
  assert.equal(sandbox.writes.length, 1);
  assert.equal(sandbox.writes[0][1].toString(), '{}');
});

test('crx header bounds are checked before slicing', () => {
  const sandbox = load();
  const buf = Buffer.alloc(16);
  buf.write('Cr24', 0);
  buf.writeUInt32LE(3, 4);
  buf.writeUInt32LE(1000, 8);
  sandbox.fs.readFileSync = () => buf;
  assert.throws(() => sandbox.api.extCrxPayload('x.crx'), /truncated/);
});
