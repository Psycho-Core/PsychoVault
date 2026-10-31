'use strict'
// Run in Electron under Xvfb with a fresh profile. This does not enter credentials or connect a VPN.
const { app, webContents } = require('electron')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-proton-smoke-'))
app.setPath('userData', profile)
const failLoads = []
app.on('web-contents-created', (_e, wc) => {
  wc.on('did-fail-load', (_e, code, description, url, main) => {
    if (main && code !== -3) failLoads.push(`${code} ${description} ${String(url).split('?')[0]}`)
  })
})
require('../app/main.js')
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
async function waitFor(label, fn, timeout = 30000) {
  const end = Date.now() + timeout
  while (Date.now() < end) {
    try { const result = await fn(); if (result) return result } catch {}
    await sleep(300)
  }
  throw new Error(label + ' timed out; load errors: ' + failLoads.join('; '))
}
async function run() {
  const ui = await waitFor('browser UI', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/index.html')))
  const before = await ui.executeJavaScript("document.querySelectorAll('#tabs [role=tab]').length")
  await ui.executeJavaScript("document.getElementById('btn-settings').click()")
  const settings = await waitFor('settings', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/settings.html')))
  const after = await ui.executeJavaScript("document.querySelectorAll('#tabs [role=tab]').length")
  if (after <= before) throw new Error('Settings did not open a new tab')
  const ext = await waitFor('Proton extension loading', () => settings.executeJavaScript(`window.pv.extensions.list().then(xs => xs.find(x => x.name === 'Proton VPN: Fast & Secure' && x.loaded))`))
  const toggle = await ui.executeJavaScript('window.pv.proton.toggle()')
  if (!toggle || toggle.ok || !/sign in/i.test(toggle.error || '')) throw new Error('signed-out VPN toggle: ' + JSON.stringify(toggle))
  console.log('PASS: signed-out VPN toggle asks for sign-in and Settings opens a new tab.')
  const result = await settings.executeJavaScript(`window.pv.extensions.openpage(${JSON.stringify({ dir: ext.dir, page: ext.popup })})`)
  if (!result || !result.ok) throw new Error('opening popup failed: ' + JSON.stringify(result))
  const popup = await waitFor('Proton popup', () => webContents.getAllWebContents().find(w => w.getURL().startsWith('chrome-extension://jplgfhpmjnbigmhklmmbgecoobifkmpa/popup.html')))
  await waitFor('visible sign-in button', () => popup.executeJavaScript(`(() => { const b = document.querySelector('.sign-in-button'); return !!b && b.offsetWidth > 0 && b.textContent.trim().length > 0 })()`), 45000)
  const layout = await waitFor('centered Proton popup', () => popup.executeJavaScript(`(() => {
    const r = document.querySelector('.main-area').getBoundingClientRect();
    return Math.abs((r.left + r.right) / 2 - innerWidth / 2) < 3 && r.width <= 520 && innerWidth >= 520
  })()`))
  if (!layout) throw new Error('Proton popup is not centered')
  await waitFor('Proxy Switch diagnostic explanation', () => settings.executeJavaScript(`(() => { const el = document.querySelector('#proxySwitchDetails'); return el && !el.hidden && /not Proton VPN/.test(el.textContent) })()`))
  console.log('PASS: Proton popup centered and Proxy Switch labeled as an advanced diagnostic.')
  // Simulate Proton's fixed-server rule, including the deliberate account bypass. No
  // credentials are used and no request is sent to the fake proxy.
  const setResult = await popup.executeJavaScript(`new Promise(resolve => chrome.proxy.settings.set({value: { mode: 'fixed_servers', rules: {singleProxy: {scheme: 'https', host: 'proxy.example.invalid', port: 4443}, bypassList: ['account.proton.me', 'localhost']}}, scope: 'regular'}, () => chrome.proxy.settings.get({}, resolve)))`)
  if (!setResult || setResult.levelOfControl !== 'controlled_by_this_extension') throw new Error('proxy not reported as controlled after setting: ' + JSON.stringify(setResult))
  const browsingSession = require('electron').session.fromPartition('persist:psychovault')
  const normal = await browsingSession.resolveProxy('https://example.com/')
  const account = await browsingSession.resolveProxy('https://account.proton.me/')
  if (!/proxy\.example\.invalid/i.test(normal) || !/^DIRECT/i.test(account)) throw new Error('unexpected proxy resolution: browsing=' + normal + ' account=' + account)
  console.log('PASS: Proton-style proxy applied to browsing while the account domain is bypassed.')
  await popup.executeJavaScript(`new Promise(resolve => chrome.proxy.settings.clear({}, resolve))`)
  const permissions = await popup.executeJavaScript(`Promise.all([browser.permissions.getAll(), browser.permissions.contains({ permissions: ['proxy'], origins: ['http://*/*','https://*/*','ftp://*/*','ws://*/*','wss://*/*','https://account.protonvpn.com/*','https://account.proton.me/*'] })]).then(([all, contains]) => ({ all, contains })).catch(e => ({ error: String(e) }))`)
  console.error('::warning::Proton permissions: ' + JSON.stringify(permissions))
  const point = await popup.executeJavaScript(`(() => { const r = document.querySelector('.sign-in-button').getBoundingClientRect(); return { x: Math.round(r.left + r.width / 2), y: Math.round(r.top + r.height / 2) } })()`)
  popup.focus()
  popup.sendInputEvent({ type: 'mouseMove', x: point.x, y: point.y })
  popup.sendInputEvent({ type: 'mouseDown', x: point.x, y: point.y, button: 'left', clickCount: 1 })
  popup.sendInputEvent({ type: 'mouseUp', x: point.x, y: point.y, button: 'left', clickCount: 1 })
  await waitFor('Proton authorize tab', () => webContents.getAllWebContents().find(w => w.getURL().startsWith('https://account.proton.me/authorize')), 30000)
  console.log('PASS: real Proton popup opened the account authorization tab (no credentials entered).')
}
run().then(() => { app.exit(0) }).catch(error => {
  console.error('::error::Proton smoke test: ' + String(error.message).replace(/\n/g, ' '))
  try {
    const log = fs.readFileSync(path.join(profile, 'ext-shim.log'), 'utf8').split('\n').slice(-12)
    for (const line of log) if (line && !/authorize|state=|token/i.test(line)) console.error('::error::' + line.replace(/\n/g, ' '))
  } catch {}
  app.exit(1)
})
