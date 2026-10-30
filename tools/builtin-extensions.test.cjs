'use strict'
// Every bundled extension is installed into the profile at startup, so a malformed one is a
// startup failure for the user rather than a compile error. These checks catch that, and
// keep the Proxy Switch reference extension honest - it exists to prove the chrome.proxy
// path works, so it must not depend on anything but that API.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const BUILTIN = path.join(__dirname, '..', 'app', 'builtin')
const dirs = fs.readdirSync(BUILTIN, { withFileTypes: true })
  .filter((d) => d.isDirectory())
  .map((d) => d.name)

test('there is more than one bundled extension, so the concept is exercised', () => {
  assert.ok(dirs.length >= 2, `only found: ${dirs.join(', ')}`)
})

for (const name of dirs) {
  test(`${name}: manifest is valid MV3 and every file it references exists`, () => {
    const dir = path.join(BUILTIN, name)
    const manifestPath = path.join(dir, 'manifest.json')
    assert.ok(fs.existsSync(manifestPath), `${name} has no manifest.json`)
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'))
    assert.strictEqual(manifest.manifest_version, 3, `${name} is not MV3`)
    assert.ok(manifest.name && manifest.version, `${name} has no name or version`)

    const refs = []
    if (manifest.background && manifest.background.service_worker) refs.push(manifest.background.service_worker)
    if (manifest.action && manifest.action.default_popup) refs.push(manifest.action.default_popup)
    for (const file of refs) {
      assert.ok(fs.existsSync(path.join(dir, file)), `${name}: manifest points at a missing ${file}`)
    }
  })
}

test('proxy-switch only relies on the two APIs PsychoVault supplies', () => {
  const dir = path.join(BUILTIN, 'proxy-switch')
  const manifest = JSON.parse(fs.readFileSync(path.join(dir, 'manifest.json'), 'utf8'))
  const allowed = new Set(['proxy', 'storage'])
  for (const perm of manifest.permissions || []) {
    assert.ok(allowed.has(perm), `proxy-switch asks for ${perm}, which is outside what it needs`)
  }
  const js = ['background.js', 'popup.js']
    .map((f) => fs.readFileSync(path.join(dir, f), 'utf8'))
    .join('\n')
  assert.match(js, /chrome\.proxy\.settings\.set/)
  assert.match(js, /chrome\.proxy\.settings\.get/)
  assert.match(js, /chrome\.proxy\.settings\.clear/)
  // no inline handlers: the popup is a page and must not need a relaxed CSP
  const html = fs.readFileSync(path.join(dir, 'popup.html'), 'utf8')
  assert.ok(!/\son\w+=/i.test(html), 'popup.html uses an inline event handler')
  assert.ok(!/<script>(?![^<]*src=)/i.test(html), 'popup.html has an inline script')
})

test('both build paths verify the bundled extensions they ship', () => {
  const root = path.join(__dirname, '..')
  const sh = fs.readFileSync(path.join(root, 'tools', 'build-installer.sh'), 'utf8')
  const bat = fs.readFileSync(path.join(root, 'PsychoVault_easy_build.bat'), 'latin1')
  for (const name of dirs) {
    assert.ok(sh.includes(`builtin/${name}/manifest.json`), `build-installer.sh does not verify ${name}`)
    assert.ok(bat.includes(`builtin\\${name}\\manifest.json`), `.bat VERIFY list omits ${name}`)
  }
})
