'use strict'
// Check the MV3 API uBlock Origin Lite needs using a synthetic static rule. This is
// not uBlock Origin Lite itself, and passing this alone does not prove it works.
const { app, BrowserWindow, session } = require('electron')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-dnr-smoke-'))
app.setPath('userData', profile)
async function run() {
  await app.whenReady()
  const ses = session.fromPartition('persist:ubol-test')
  const dir = path.join(profile, 'dnr-test')
  fs.mkdirSync(dir, { recursive: true })
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify({
    manifest_version: 3, name: 'MV3 static blocking test', version: '1.0.0',
    permissions: ['declarativeNetRequest'], host_permissions: ['<all_urls>'],
    declarative_net_request: { rule_resources: [{ id: 'test', enabled: true, path: 'rules.json' }] }
  }))
  fs.writeFileSync(path.join(dir, 'rules.json'), JSON.stringify([{
    id: 1, priority: 1, action: { type: 'block' },
    condition: { urlFilter: '||pv-ubol-test.invalid^', resourceTypes: ['main_frame'] }
  }]))
  await ses.extensions.loadExtension(dir)
  const win = new BrowserWindow({ show: false, webPreferences: { session: ses, sandbox: true } })
  try {
    await win.loadURL('http://pv-ubol-test.invalid/')
    throw new Error('the test URL was not blocked')
  } catch (e) {
    if (!String(e.message).includes('ERR_BLOCKED_BY_CLIENT')) throw new Error('static rule did not block the request: ' + e.message)
  }
  console.log('PASS: a synthetic MV3 declarativeNetRequest static rule blocked a request.')
}
run().then(() => app.exit(0)).catch(error => {
  console.error('::error::uBlock Lite compatibility probe: ' + String(error.message).replace(/\n/g, ' '))
  app.exit(1)
})
