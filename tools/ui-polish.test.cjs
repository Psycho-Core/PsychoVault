'use strict'
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const read = p => fs.readFileSync(path.join(__dirname, '..', p), 'utf8')
const main = read('app/main.js'), ui = read('app/ui/renderer.js'), html = read('app/ui/index.html'), settings = read('app/ui/settings.html')

test('workspace toggle retains snapshots and puts current tabs into a separate group when reenabled', () => {
  assert.match(main, /workspacesEnabled: true/)
  assert.match(main, /settings\.workspacesEnabled !== false\) \{\s*workspaces\.list\[workspaces\.active\]/)
  assert.match(main, /workspaces\.list\.push\(\{ name: 'Current tabs', \.\.\.w\.wsState\(\) \}\)/)
  assert.match(ui, /isPrivate \|\| state\.settings\?\.workspacesEnabled === false/)
  assert.match(settings, /id="workspacesEnabled"/)
})
test('menu contains the three panels without sidebar buttons or stale indexed references', () => {
  for (const name of ['bookmarks', 'history', 'downloads']) {
    assert.doesNotMatch(html, new RegExp('class="rail-btn" data-panel="' + name + '"'))
    assert.match(html, new RegExp('data-cmd="' + name + '"'))
  }
  assert.doesNotMatch(ui, /railBtns\[\d\]/)
})
test('toast space is reserved and released, About uses app version, logs are accessible', () => {
  assert.match(ui, /extraBottom: toastHeight/)
  assert.match(ui, /t\.classList\.add\('hidden'\); syncChrome\(\)/)
  assert.match(main, /Math\.max\(STATUS_H, this\.extraBottom\)/)
  assert.doesNotMatch(settings, /PsychoVault 1\.0\.4/)
  assert.match(settings, /appVersion.*PsychoVault/)
  assert.match(main, /data:openErrorLog/)
  assert.match(settings, /redact them before sharing/)
})
test('installer welcome and finish text uses NSIS line break escapes', () => {
  const nsi = read('installer.nsi')
  for (const line of nsi.split('\n').filter(x => /^!define MUI_(WELCOME|FINISH)PAGE_TEXT/.test(x))) {
    assert.doesNotMatch(line, /(?<!\$)\\r/)
    assert.match(line, /\$\\r\$\\n/)
  }
})
test('native hamburger menu does not alter the page layout', () => {
  assert.match(main, /ipcMain\.handle\('ui:menu'/)
  assert.doesNotMatch(ui, /extra \+= MENU_H/)
  assert.match(ui, /window\.pv\.ui\.menu\(\)/)
  assert.match(read('app/preload.js'), /'menu-command'/)
})
test('diagnostics report only contains an explicit allowlist and log controls are in About', () => {
  const report = main.slice(main.indexOf('function diagnosticReport()'), main.indexOf("ipcMain.handle('app:info'"))
  assert.doesNotMatch(report, /readFile|settings\.extensions|history|url|token|password/i)
  assert.match(settings.slice(settings.indexOf('data-category-panel="about"')), /id="openErrorLog"/)
  assert.match(settings, /id="diagnosticsCopy"/)
})
