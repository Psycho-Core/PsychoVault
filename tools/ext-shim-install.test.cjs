'use strict'
// Guards on app/ext-shim.js. The file cannot be required here (it imports electron and runs
// inside an extension's own JavaScript world), so the things that actually decide whether a
// VPN extension can route traffic are asserted against the source.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const src = fs.readFileSync(path.join(__dirname, '..', 'app', 'ext-shim.js'), 'utf8')

test('the shim does not skip installation when chrome.proxy already exists', () => {
  // Chromium exposes its own chrome.proxy wired to an extension function dispatcher
  // Electron does not implement, so "it exists" is true and useless: Proton's
  // settings.set reached Chromium, logged "Unknown Extension API - types.ChromeSetting.set"
  // and was refused. Guarding on existence meant the shim never installed anything.
  assert.ok(!/if\s*\(\s*!chromeObj\.proxy\s*\)/.test(src), 'chrome.proxy is still guarded on existence')
  assert.ok(!/if\s*\(\s*!chromeObj\.privacy\s*\)/.test(src), 'chrome.privacy is still guarded on existence')
})

test('both APIs are installed through the override helper, unconditionally', () => {
  assert.match(src, /proxyInstalled = override\('proxy',/)
  assert.match(src, /privacyInstalled = override\('privacy',/)
  assert.strictEqual((src.match(/override\('proxy'/g) || []).length, 1)
  assert.strictEqual((src.match(/override\('privacy'/g) || []).length, 1)
})

test('the override replaces the property rather than assigning over it', () => {
  assert.match(src, /Object\.defineProperty\(chromeObj, name,/)
  // a non-configurable Chromium property needs the fallback to be attempted too
  assert.match(src, /chromeObj\[name\] = value/)
  // configurable is what allows replacing an existing accessor
  assert.match(src, /configurable: true/)
})

test('the result of the injection is reported to the main process', () => {
  assert.match(src, /b\.report\(\{ proxy: proxyInstalled, privacy: privacyInstalled \}\)/)
  assert.match(src, /report: \(info\) => ipcRenderer\.invoke\('extshim:report'/)
})

test('a failed injection is not swallowed silently', () => {
  // if defineProperty and assignment both fail, proxyInstalled is false and the main process
  // logs it; the Settings readout shows it too
  const main = fs.readFileSync(path.join(__dirname, '..', 'app', 'main.js'), 'utf8')
  assert.match(main, /extshim:report/)
  assert.match(main, /if \(!extShimReport\.proxy\) logMainError/)
  assert.match(main, /shim: extShimReport/)
})
