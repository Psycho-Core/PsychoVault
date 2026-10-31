'use strict'
// Exercise Chromium's extension auth callback through an actual local proxy challenge.
// This test uses synthetic credentials only; it does not contact Proton or the internet.
const { app, BrowserWindow, session } = require('electron')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const http = require('node:http')
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-proxy-auth-'))
app.setPath('userData', profile)
require('../app/main.js')
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
async function waitFor(label, fn, timeout = 30000) {
  const end = Date.now() + timeout
  while (Date.now() < end) {
    try { const v = await fn(); if (v) return v } catch {}
    await sleep(250)
  }
  throw new Error(label + ' timed out')
}
async function run() {
  await app.whenReady()
  const ses = session.fromPartition('persist:psychovault')
  await waitFor('preloads', () => ses.getPreloadScripts().some(p => p.id === 'pv-ext-sw'))
  let challenges = 0, accepted = 0
  const proxy = http.createServer((req, res) => {
    if (req.headers['proxy-authorization'] !== 'Basic ' + Buffer.from('test-user:test-pass').toString('base64')) {
      challenges++
      res.writeHead(407, { 'Proxy-Authenticate': 'Basic realm="PsychoVault test"', 'Content-Length': '0' })
      res.end()
      return
    }
    accepted++
    res.writeHead(200, { 'Content-Type': 'text/html' })
    res.end('<title>proxy auth succeeded</title>')
  })
  await new Promise(resolve => proxy.listen(0, '127.0.0.1', resolve))
  const port = proxy.address().port
  const dir = path.join(profile, 'auth-extension')
  fs.mkdirSync(dir, { recursive: true })
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify({ manifest_version: 3, name: 'Proxy auth smoke', version: '1.0.0', permissions: ['proxy', 'storage', 'webRequest', 'webRequestAuthProvider'], host_permissions: ['<all_urls>'], background: { service_worker: 'worker.js' } }))
  fs.writeFileSync(path.join(dir, 'worker.js'), `
chrome.webRequest.onAuthRequired.addListener((details, callback) => {
  if (!details.isProxy) return callback({});
  callback({authCredentials:{username:'test-user',password:'test-pass'}});
}, {urls:['<all_urls>']}, ['asyncBlocking']);
setTimeout(() => chrome.proxy.settings.set({value:{mode:'fixed_servers',rules:{singleProxy:{scheme:'http',host:'127.0.0.1',port:${port}}}}}, () => chrome.proxy.settings.get({}, result => chrome.storage.local.set({ready: result || {error: chrome.runtime.lastError && chrome.runtime.lastError.message}}))), 1500);`)
  fs.writeFileSync(path.join(dir, 'test.html'), '<title>test extension</title>')
  const ext = await ses.extensions.loadExtension(dir)
  const win = new BrowserWindow({ show: false, webPreferences: { session: ses, contextIsolation: true, sandbox: true } })
  await win.loadURL('chrome-extension://' + ext.id + '/test.html')
  const setup = await waitFor('worker setup', () => win.webContents.executeJavaScript(`new Promise(resolve => chrome.storage.local.get('ready', r => resolve(r && r.ready)))`))
  const resolved = await ses.resolveProxy('http://proxy-auth-test.invalid/')
  const page = new BrowserWindow({ show: false, webPreferences: { session: ses, sandbox: true } })
  try {
    await Promise.race([page.loadURL('http://proxy-auth-test.invalid/'), sleep(20000).then(() => { throw new Error('proxy page load timed out') })])
  } catch (e) {
    throw new Error(String(e.message) + `; resolved=${resolved}; challenges=${challenges}; accepted=${accepted}; setup=${JSON.stringify(setup)}`)
  }
  const title = page.webContents.getTitle()
  if (!challenges || !accepted || title !== 'proxy auth succeeded') throw new Error(`proxy challenge failed: challenges=${challenges} accepted=${accepted} title=${title}`)
  console.log('PASS: extension supplied synthetic credentials for an actual local proxy challenge; the page loaded through that proxy.')
  proxy.close()
}
run().then(() => app.exit(0)).catch(error => {
  console.error('::error::Proxy authentication test: ' + String(error.message).replace(/\n/g, ' '))
  try { for (const line of fs.readFileSync(path.join(profile, 'ext-shim.log'), 'utf8').split('\n').slice(-15)) if (line && !/authorize|state=|token/i.test(line)) console.error('::error::' + line) } catch {}
  app.exit(1)
})
