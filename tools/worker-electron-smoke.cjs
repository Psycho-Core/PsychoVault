'use strict'
// This runs on Windows in the real Electron runtime, with a fresh profile and a throwaway
// extension. It never contacts the fake proxy and never uses Proton credentials.
const { app, BrowserWindow, session } = require('electron')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-worker-smoke-'))
app.setPath('userData', profile)
// The synthetic worker controls this isolated profile; bundled extensions must not interfere.
fs.writeFileSync(path.join(profile, 'builtin-removed.json'), JSON.stringify(['proton-vpn', 'dark-reader', 'proxy-switch']))
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
  await waitFor('extension preload registration', () => ses.getPreloadScripts().some(p => p.id === 'pv-ext-sw'))
  const dir = path.join(profile, 'test-worker')
  fs.mkdirSync(dir, { recursive: true })
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify({
    manifest_version: 3, name: 'Worker proxy smoke test', version: '1.0.0',
    permissions: ['proxy', 'storage'], background: { service_worker: 'worker.js' }
  }))
  fs.writeFileSync(path.join(dir, 'worker.js'), `
setTimeout(() => {
  try {
    chrome.proxy.settings.set({value:{mode:'fixed_servers',rules:{singleProxy:{scheme:'https',host:'proxy.example.invalid',port:4443},bypassList:['account.proton.me']}}}, () => {
      const setError = chrome.runtime.lastError && chrome.runtime.lastError.message;
      chrome.proxy.settings.get({}, result => chrome.storage.local.set({workerResult:{result, error: setError || (chrome.runtime.lastError && chrome.runtime.lastError.message)}}));
    });
  } catch (e) { chrome.storage.local.set({workerResult:{error:String(e)}}); }
}, 1500);`)
  fs.writeFileSync(path.join(dir, 'test.html'), '<!doctype html><title>worker test</title>')
  ses.serviceWorkers.on('console-message', (_event, info) => {
    if (info.sourceUrl && info.sourceUrl.includes('worker.js')) console.error('::warning::worker console: ' + info.message)
  })
  ses.serviceWorkers.on('running-status-changed', info => {
    try {
      const worker = ses.serviceWorkers.getWorkerFromVersionID(info.versionId)
      if (worker && worker.scope.includes('chrome-extension://')) console.error('::warning::worker status: ' + info.runningStatus + ' ' + worker.scope)
    } catch {}
  })
  const ext = await ses.extensions.loadExtension(dir)
  const win = new BrowserWindow({ show: false, webPreferences: { session: ses, contextIsolation: true, sandbox: true } })
  await win.loadURL('chrome-extension://' + ext.id + '/test.html')
  const response = await waitFor('worker proxy result', () =>
    win.webContents.executeJavaScript(`new Promise(resolve => chrome.storage.local.get('workerResult', r => resolve(r && r.workerResult)))`), 20000)
  if (!response || response.error || !response.result || response.result.levelOfControl !== 'controlled_by_this_extension') {
    throw new Error('worker proxy result: ' + JSON.stringify(response))
  }
  const normal = await ses.resolveProxy('https://example.com/')
  const account = await ses.resolveProxy('https://account.proton.me/')
  if (!/proxy\.example\.invalid/i.test(normal) || !/^DIRECT/i.test(account)) {
    throw new Error('unexpected proxy resolution: browsing=' + normal + ' account=' + account)
  }
  console.log('PASS: MV3 worker set the proxy through the bridge, browsing resolved to it, and the account domain bypassed it.')
}
run().then(() => app.exit(0)).catch(error => {
  console.error('::error::Worker proxy test: ' + String(error.message).replace(/\n/g, ' '))
  try {
    for (const line of fs.readFileSync(path.join(profile, 'ext-shim.log'), 'utf8').split('\n').slice(-20)) {
      if (line && !/authorize|state=|token/i.test(line)) console.error('::error::' + line)
    }
  } catch {}
  app.exit(1)
})
