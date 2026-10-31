'use strict'
const test = require('node:test'), assert = require('node:assert/strict')
const { captureWindows, selectWindows } = require('../app/session-recovery')
test('only normal windows and web URLs are captured; credentials are stripped', () => {
  const tab = { url: 'https://u:p@example.com/path', title: 'test' }
  const normal = { private: false, order: [1,2], tabs: new Map([[1, tab],[2,{url:'file:///secret'}]]) }
  const privateWin = { private: true, order: [1], tabs: new Map([[1, tab]]) }
  const snapshot = captureWindows([normal, privateWin])
  assert.equal(snapshot.length, 1); assert.equal(snapshot[0].tabs.length, 1); assert.equal(snapshot[0].tabs[0].url, 'https://example.com/path')
  assert.deepEqual(selectWindows(snapshot, [[]]), [])
  assert.equal(selectWindows(snapshot, [[0]])[0].tabs[0].url, 'https://example.com/path')
})
