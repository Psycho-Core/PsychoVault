'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { recordedDownloadPath } = require('../app/download-open');

test('open/reveal only accepts a path recorded by the download manager', () => {
  const recorded = path.resolve('downloads/report.pdf');
  const downloads = [{ path: recorded }, { path: '' }, null];
  assert.equal(recordedDownloadPath(downloads, recorded), recorded);
  assert.equal(recordedDownloadPath(downloads, path.join(path.dirname(recorded), '.', 'report.pdf')), recorded);
  assert.equal(recordedDownloadPath(downloads, path.resolve('downloads/other.pdf')), null);
  assert.equal(recordedDownloadPath(downloads, path.resolve('PsychoVault.exe')), null);
  assert.equal(recordedDownloadPath(downloads, ''), null);
  assert.equal(recordedDownloadPath(null, recorded), null);
});
