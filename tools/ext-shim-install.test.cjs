'use strict'
// Guards on app/ext-shim.js. It cannot be required here (it imports electron and runs inside an
// extension context), so the things that decide whether a VPN extension can route traffic are
// asserted against the source.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const root = path.join(__dirname, '..', 'app')
const src = fs.readFileSync(path.join(root, 'ext-shim.js'), 'utf8')
const main = fs.readFileSync(path.join(root, 'main.js'), 'utf8')

// The installer body: from its declaration to the line after its closing brace, before the
// delivery code that follows it.
const installerAt = src.indexOf('function installExtensionAPIs')
const installer = src.slice(installerAt, src.indexOf('const url = currentURL()'))

test('an extension preload requires nothing but electron', () => {
  // Field report: "Unable to load preload script: ...ext-shim.js / Error: module not found: fs".
  // Extension preloads are sandboxed - require('fs') fails the entire preload, so no bridge, no
  // log and no APIs were installed at all. Anything else has to arrive through ipcRenderer.
  const requires = src.match(/require\(['"][^'"]+['"]\)/g) || []
  assert.ok(requires.length > 0, 'no require at all - did the file become a plain script?')
  for (const r of requires) {
    assert.strictEqual(r, "require('electron')", 'a sandboxed extension preload cannot load ' + r)
  }
  assert.ok(!/\bimport\s+.*\bfrom\b/.test(src), 'ext-shim.js uses an import statement')
})

test('the shim does not skip installation when chrome.proxy already exists', () => {
  // Chromium exposes its own chrome.proxy wired to an extension function dispatcher Electron
  // does not implement, so "it exists" is true and useless: Proton's settings.set reached
  // Chromium, logged "Unknown Extension API - types.ChromeSetting.set" and was refused.
  assert.ok(!/if\s*\(\s*!chromeObj\.proxy\s*\)/.test(installer), 'chrome.proxy is still guarded on existence')
  assert.ok(!/if\s*\(\s*!chromeObj\.privacy\s*\)/.test(installer), 'chrome.privacy is still guarded on existence')
})

test('all three APIs are installed through the override helper, unconditionally', () => {
  assert.match(installer, /var proxyInstalled = override\('proxy',/)
  assert.match(installer, /var privacyInstalled = override\('privacy',/)
  assert.match(installer, /var tabsInstalled = override\('tabs',/)
  assert.match(installer, /Object\.defineProperty\(chromeObj, name,/)
  assert.match(installer, /chromeObj\[name\] = value/)
  assert.match(installer, /configurable: true/)
})

test('the installer can be stringified into another world', () => {
  // It is delivered with Function.prototype.toString, so it must not close over anything -
  // a reference to a variable from this file would be a ReferenceError in the page.
  assert.match(src, /installExtensionAPIs\.toString\(\)/)
  // Comments are not code: the body's own comments talk about the bridge.
  const body = installer
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .split('\n')
    .filter((l) => !l.trim().startsWith('//'))
    .join('\n')
    .slice(installer.slice(0, installer.indexOf('{')).length)
  for (const outer of ['BRIDGE_NAME', 'isServiceWorker', 'note(', 'ipcRenderer', 'contextBridge']) {
    assert.ok(!new RegExp('\\b' + outer.replace('(', '\\(')).test(body), 'the installer closes over ' + outer)
  }
})

test('the page path verifies the install instead of assuming it', () => {
  // "I ran some code" is not "chrome.proxy is mine". The result is read back from that world
  // and written to the log, which is what turns a silent no-op into a reported one.
  assert.match(src, /webFrame\.executeJavaScript\(source\)/)
  assert.match(src, /__pvShimInstalled/)
  assert.match(src, /RAN BUT DID NOT TAKE/)
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
  assert.match(swBlock, /installExtensionAPIs\(globalThis, BRIDGE_NAME\)/)
  assert.ok(!/contextBridge\.executeInMainWorld/.test(swBlock), 'the service worker path still calls executeInMainWorld')
  assert.match(src, /globalThis\[BRIDGE_NAME\] = bridge/, 'no fallback when defineProperty is refused')
})

test('a failed injection is not swallowed silently', () => {
  assert.match(main, /extshim:report/)
  assert.match(main, /if \(!extShimReport\.proxy\) logMainError/)
  assert.match(main, /shim: extShimReport/)
})

test('what the shim did is written to a log file, not just guessed at', () => {
  assert.match(src, /ipcRenderer\.invoke\('extshim:log'/)
  assert.match(main, /const EXT_SHIM_LOG = path\.join\(USER, 'ext-shim\.log'\)/)
  assert.match(main, /fs\.appendFileSync\(EXT_SHIM_LOG/)
  assert.match(main, /shimLog: EXT_SHIM_LOG/)
  const allow = main.slice(main.indexOf('const EXT_SHIM_CHANNELS'), main.indexOf(']', main.indexOf('const EXT_SHIM_CHANNELS')))
  assert.ok(allow.includes("'extshim:log'"), 'the log channel is not in the allow-list')
})
