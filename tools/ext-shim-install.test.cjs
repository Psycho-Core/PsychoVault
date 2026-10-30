'use strict'
// Guards on the extension shim, which is now two files:
//   app/ext-shim.js       - preload: bridge + delivery of the installer into the extension's world
//   app/ext-main-world.js - the APIs themselves, no imports, runs in that world
// Neither can be required here (they run inside an extension context), so the things that
// decide whether a VPN extension can route traffic are asserted against the source.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const root = path.join(__dirname, '..', 'app')
const src = fs.readFileSync(path.join(root, 'ext-shim.js'), 'utf8')
const world = fs.readFileSync(path.join(root, 'ext-main-world.js'), 'utf8')
const main = fs.readFileSync(path.join(root, 'main.js'), 'utf8')

test('the shim does not skip installation when chrome.proxy already exists', () => {
  // Chromium exposes its own chrome.proxy wired to an extension function dispatcher Electron
  // does not implement, so "it exists" is true and useless: Proton's settings.set reached
  // Chromium, logged "Unknown Extension API - types.ChromeSetting.set" and was refused.
  assert.ok(!/if\s*\(\s*!chromeObj\.proxy\s*\)/.test(world), 'chrome.proxy is still guarded on existence')
  assert.ok(!/if\s*\(\s*!chromeObj\.privacy\s*\)/.test(world), 'chrome.privacy is still guarded on existence')
})

test('both APIs are installed through the override helper, unconditionally', () => {
  assert.match(world, /proxyInstalled = override\('proxy',/)
  assert.match(world, /privacyInstalled = override\('privacy',/)
  assert.strictEqual((world.match(/override\('proxy'/g) || []).length, 1)
  assert.strictEqual((world.match(/override\('privacy'/g) || []).length, 1)
})

test('the override replaces the property rather than assigning over it', () => {
  assert.match(world, /Object\.defineProperty\(chromeObj, name,/)
  assert.match(world, /chromeObj\[name\] = value/)
  assert.match(world, /configurable: true/)
})

test('the result of the injection is reported to the main process', () => {
  assert.match(world, /b\.report\(\{ proxy: proxyInstalled, privacy: privacyInstalled, tabs: tabsInstalled, where: where \}\)/)
  assert.match(src, /report: \(info\) => ipcRenderer\.invoke\('extshim:report'/)
})

test('a failed injection is not swallowed silently', () => {
  assert.match(main, /extshim:report/)
  assert.match(main, /if \(!extShimReport\.proxy\) logMainError/)
  assert.match(main, /shim: extShimReport/)
})

test('each delivery path is tried, and the installer is not tied to one of them', () => {
  // webFrame.executeJavaScript reaches an extension page's main world - the world that has
  // `chrome`. executeInMainWorld is kept as a second attempt because it was the only path for
  // three versions and produced "Unknown Extension API" from a page that was fully rendered.
  assert.match(src, /webFrame\.executeJavaScript\(source\)/)
  assert.match(src, /inject\('webFrame'\) \|\| inject\('executeInMainWorld'\)/)
  assert.match(world, /typeof window !== 'undefined' \? 'page' : 'worker'/)
})

test('the service worker path installs directly instead of using executeInMainWorld', () => {
  const at = src.indexOf('if (isServiceWorker) {')
  assert.ok(at !== -1, 'no service worker branch')
  const swBlock = src
    .slice(at, src.indexOf('} else {', at))
    .split('\n')
    .filter((line) => !line.trim().startsWith('//'))
    .join('\n')
  assert.match(swBlock, /Object\.defineProperty\(globalThis, BRIDGE_NAME,/)
  assert.match(swBlock, /inject\('eval'\) \|\| inject\('newFunction'\)/)
  assert.ok(!/contextBridge\.executeInMainWorld/.test(swBlock), 'the service worker path still calls executeInMainWorld')
  assert.match(src, /globalThis\[BRIDGE_NAME\] = bridge/, 'no fallback when defineProperty is refused')
})

test('the installer file is plain script - no imports, nothing to resolve', () => {
  // It is delivered as a string into another world; a require() in it would throw there.
  assert.ok(!/^\s*(require|import)\b/m.test(world), 'ext-main-world.js imports something')
  assert.ok(!/\bmodule\.exports\b/.test(world), 'ext-main-world.js is a module')
})

test('what the shim did is written to a log file, not just guessed at', () => {
  assert.match(src, /ipcRenderer\.invoke\('extshim:log'/)
  assert.match(main, /const EXT_SHIM_LOG = path\.join\(USER, 'ext-shim\.log'\)/)
  assert.match(main, /fs\.appendFileSync\(EXT_SHIM_LOG/)
  assert.match(main, /shimLog: EXT_SHIM_LOG/)
  const allow = main.slice(main.indexOf('const EXT_SHIM_CHANNELS'), main.indexOf(']', main.indexOf('const EXT_SHIM_CHANNELS')))
  assert.ok(allow.includes("'extshim:log'"), 'the log channel is not in the allow-list')
})
