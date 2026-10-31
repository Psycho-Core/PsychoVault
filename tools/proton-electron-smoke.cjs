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
  await ui.executeJavaScript("window.pv.nav.go('psychovault://settings')")
  const settings = await waitFor('settings', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/settings.html')))
  const ext = await waitFor('Proton extension loading', () => settings.executeJavaScript(`window.pv.extensions.list().then(xs => xs.find(x => x.name === 'Proton VPN: Fast & Secure' && x.loaded))`))
  const result = await settings.executeJavaScript(`window.pv.extensions.openpage(${JSON.stringify({ dir: ext.dir, page: ext.popup })})`)
  if (!result || !result.ok) throw new Error('opening popup failed: ' + JSON.stringify(result))
  const popup = await waitFor('Proton popup', () => webContents.getAllWebContents().find(w => w.getURL().startsWith('chrome-extension://jplgfhpmjnbigmhklmmbgecoobifkmpa/popup.html')))
  await waitFor('visible sign-in button', () => popup.executeJavaScript(`(() => { const b = document.querySelector('.sign-in-button'); return !!b && b.offsetWidth > 0 && b.textContent.trim().length > 0 })()`), 45000)
  await popup.executeJavaScript("document.querySelector('.sign-in-button').click()")
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
