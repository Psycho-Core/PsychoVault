'use strict'
// Electron smoke using a throwaway profile and synthetic crash snapshot. No user credentials.
const { app, webContents, dialog } = require('electron')
const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http')
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-features-'))
app.setPath('userData', dir)
fs.writeFileSync(path.join(dir, 'running.marker'), 'running')
fs.writeFileSync(path.join(dir, 'crash-recovery.json'), JSON.stringify([{ tabs: [{ url: 'https://example.com/', title: 'Example', pinned: false, muted: false }] }]))
const backup = path.join(dir, 'backup.pvbackup'), csv = path.join(dir, 'passwords.csv')
fs.writeFileSync(csv, 'url,username,password\nhttps://example.com,alice,fromcsv\n')
dialog.showSaveDialog = async (_w, options) => ({ canceled: false, filePath: options.title === 'Save file' ? path.join(dir, 'download.bin') : backup })
dialog.showOpenDialog = async (_w, options) => ({ canceled: false, filePaths: [options.filters[0].extensions[0] === 'csv' ? csv : backup] })
require('../app/main.js')
const sleep = n => new Promise(resolve => setTimeout(resolve, n))
async function wait(label, fn) { const end = Date.now()+40000; while (Date.now()<end) { try { const x = await fn(); if (x) return x } catch {} await sleep(200) } throw Error(label + ' timed out') }
async function run() {
  const recovery = await wait('recovery page', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/recovery.html')))
  const snapshot = await recovery.executeJavaScript('window.pv.recovery.list()')
  if (snapshot.length !== 1 || snapshot[0].tabs[0].url !== 'https://example.com/') throw Error('Recovery snapshot mismatch')
  const restored = await recovery.executeJavaScript('window.pv.recovery.restore([[]])')
  if (!restored.ok || restored.windows !== 0) throw Error('Recovery skip failed')
  const chrome = await wait('chrome', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/index.html')))
  await chrome.executeJavaScript("document.getElementById('btn-settings').click()")
  const settings = await wait('settings', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/settings.html')))
  const result = await settings.executeJavaScript(`(async () => {
    const v = window.pv.vault
    const master = await v.setMaster('test master password')
    if (!master.ok) return { stage:'master', master }
    const added = await v.add({ site:'example.com', user:'alice', pass:'original' })
    if (!added.ok) return { stage:'add', added }
    const saved = await v.transfer('backup', 'test backup password')
    const imported = await v.transfer('csv', '')
    const restored = await v.transfer('restore', 'test backup password')
    const list = await v.list()
    return { saved, imported, restored, list }
  })()`)
  if (!result.saved?.ok || !result.imported?.ok || result.imported.skipped !== 1 || !result.restored?.ok || result.restored.skipped !== 1 || result.list.entries.length !== 1) throw Error('Vault transfer: ' + JSON.stringify(result))
  if (fs.readFileSync(backup, 'utf8').includes('original')) throw Error('Backup contains plaintext password')
  const opened = await settings.executeJavaScript('window.pv.vpncheck.open()')
  if (!opened.ok) throw Error('VPN check page failed to open')
  await wait('VPN check page', () => webContents.getAllWebContents().find(w => w.getURL().includes('/ui/vpncheck.html')))
  const server = http.createServer((_req, res) => {
    const size = 1024 * 1024; res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': size, 'Content-Disposition': 'attachment; filename="download.bin"' })
    let sent = 0; const timer = setInterval(() => { if (res.destroyed) return clearInterval(timer); const chunk = Buffer.alloc(Math.min(8192, size - sent)); sent += chunk.length; res.write(chunk); if (sent >= size) { clearInterval(timer); res.end() } }, 30)
  })
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
  try {
    settings.downloadURL('http://127.0.0.1:' + server.address().port + '/download')
    const entry = await wait('active download', () => chrome.executeJavaScript("window.pv.downloads.list().then(xs => xs.find(x => x.state === 'progress' && x.id))"))
    const paused = await chrome.executeJavaScript(`window.pv.downloads.control({id:${JSON.stringify(entry.id)},action:'pause'})`)
    if (!paused.ok) throw Error('Pause failed: ' + JSON.stringify(paused))
    await wait('paused state', () => chrome.executeJavaScript(`window.pv.downloads.list().then(xs => xs.find(x => x.id === ${JSON.stringify(entry.id)} && x.state === 'paused'))`))
    const resumed = await chrome.executeJavaScript(`window.pv.downloads.control({id:${JSON.stringify(entry.id)},action:'resume'})`)
    if (!resumed.ok) throw Error('Resume failed: ' + JSON.stringify(resumed))
    await wait('download complete', () => chrome.executeJavaScript(`window.pv.downloads.list().then(xs => xs.find(x => x.id === ${JSON.stringify(entry.id)} && x.state === 'completed'))`))
    if (fs.statSync(path.join(dir, 'download.bin')).size !== 1024 * 1024) throw Error('download size mismatch')
  } finally { server.close() }
  console.log('PASS: crash recovery, encrypted vault transfers, VPN check page and local download pause/resume')
}
run().then(() => app.exit(0)).catch(e => { console.error('::error::' + e.stack); app.exit(1) })
