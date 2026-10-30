'use strict'
// Tests for the window lookup. The case that matters is the one that shipped broken:
// Settings is a tab, so its IPC sender is a tab view's webContents, not the window's chrome
// webContents. Matching only the latter made every extension button in Settings report
// "no window to open it in" while a window was plainly open.
const test = require('node:test')
const assert = require('node:assert')
const { findWindowFor } = require('../app/window-lookup.js')

const fakeWindow = (chromeContents, tabViews = [], { map = true } = {}) => {
  const tabs = tabViews.map((view, i) => ({ id: i + 1, view }))
  return {
    win: { webContents: chromeContents },
    tabs: map ? new Map(tabs.map((t) => [t.id, t])) : Object.fromEntries(tabs.map((t) => [t.id, t]))
  }
}

test('the chrome UI resolves to its own window', () => {
  const chrome = { name: 'chrome' }
  const w = fakeWindow(chrome)
  assert.strictEqual(findWindowFor(chrome, [w]), w)
})

test('a tab view resolves to its owning window - the case that was broken', () => {
  const chrome = { name: 'chrome' }
  const settingsView = { name: 'settings-tab' }
  const w = fakeWindow(chrome, [{ webContents: settingsView }])
  assert.strictEqual(findWindowFor(settingsView, [w]), w)
})

test('a page in the second window resolves to the second window, not the first', () => {
  const pageView = { name: 'page' }
  const first = fakeWindow({ name: 'chrome-1' }, [{ webContents: { name: 'tab-1' } }])
  const second = fakeWindow({ name: 'chrome-2' }, [{ webContents: pageView }])
  assert.strictEqual(findWindowFor(pageView, [first, second]), second)
})

test('tabs may be a plain object as well as a Map', () => {
  const view = { name: 'v' }
  const w = fakeWindow({ name: 'chrome' }, [{ webContents: view }], { map: false })
  assert.strictEqual(findWindowFor(view, [w]), w)
})

test('an unknown sender resolves to nothing rather than to the first window', () => {
  const w = fakeWindow({ name: 'chrome' }, [{ webContents: { name: 'tab' } }])
  assert.strictEqual(findWindowFor({ name: 'stranger' }, [w]), null)
  assert.strictEqual(findWindowFor(null, [w]), null)
  assert.strictEqual(findWindowFor(undefined, [w]), null)
})

test('a destroyed window or view throws nothing and is skipped', () => {
  const page = { name: 'page' }
  const good = fakeWindow({ name: 'chrome-ok' }, [{ webContents: page }])
  const broken = {
    get win() { throw new Error('Object has been destroyed') },
    get tabs() { throw new Error('Object has been destroyed') }
  }
  assert.strictEqual(findWindowFor(page, [broken, good]), good)
})

test('an empty window list is not an error', () => {
  assert.strictEqual(findWindowFor({ name: 'x' }, []), null)
  assert.strictEqual(findWindowFor({ name: 'x' }, undefined), null)
})
