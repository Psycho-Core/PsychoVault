'use strict'
// Real Dark Reader with a throwaway profile and local page - no user data or network services.
const { app, webContents, session } = require('electron')
const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http')
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-dark-reader-'))
app.setPath('userData', dir)
fs.writeFileSync(path.join(dir, 'builtin-removed.json'), JSON.stringify(['proton-vpn', 'proxy-switch']))
require('../app/main.js')
const sleep = ms => new Promise(r => setTimeout(r, ms))
async function wait(label, f) { const until = Date.now() + 50000; while (Date.now() < until) { try { const v = await f(); if (v) return v } catch {} await sleep(250) } throw new Error(label + ' timed out') }
async function run() {
  const server = http.createServer((_req, res) => { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<!doctype html><html><body style="background:white;color:black">Light test page</body></html>') })
  await new Promise(r => server.listen(0, '127.0.0.1', r))
  try {
    await app.whenReady()
    const ses = session.fromPartition('persist:psychovault')
    ses.serviceWorkers.on('console-message', (_event, info) => { if (info.message) console.error('::warning::worker ' + String(info.message).slice(0, 300)) })
    const chrome = await wait('chrome', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/index.html')))
    await chrome.executeJavaScript("document.querySelector('#btn-settings').click()")
    const settings = await wait('settings page', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/settings.html')))
    const ext = await wait('Dark Reader extension', () => settings.executeJavaScript("window.pv.extensions.list().then(xs => xs.find(x => x.builtin === 'dark-reader' && x.id && x.loaded))"))
    await chrome.executeJavaScript(`window.pv.tab.new(${JSON.stringify('http://127.0.0.1:' + server.address().port + '/')})`)
    const page = await wait('test page', () => webContents.getAllWebContents().find(w => w.getURL().startsWith('http://127.0.0.1:' + server.address().port)))
    page.on('console-message', (_event, info) => console.error('::warning::page ' + String(info.message || '').slice(0, 300)))
    try { await wait('Dark Reader styles on local page', () => page.executeJavaScript("!!document.querySelector('style.darkreader, meta[name=darkreader]')")) }
    catch (e) { console.error('::warning::page state ' + JSON.stringify(await page.executeJavaScript("({html:document.documentElement.outerHTML.slice(0, 1200),url:location.href})"))); throw e }

    const popup = await settings.executeJavaScript(`window.pv.extensions.openpage({dir:${JSON.stringify(ext.dir)}, page:${JSON.stringify(ext.popup)}})`)
    if (!popup.ok) throw Error('Dark Reader popup did not open')
    console.log('PASS: bundled Dark Reader loaded, injected styles on local page and opened its popup')
  } finally { server.close() }
}
run().then(() => app.exit(0)).catch(e => { console.error('::error::' + e.stack); app.exit(1) })
