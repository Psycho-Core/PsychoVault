'use strict'
// The installer lists every payload file explicitly instead of asking NSIS to enumerate a
// folder, because `File /r "dir/*.*"` reported "no files found" for a folder whose
// LICENSE.txt the same script had just read. These checks keep the generator honest.
const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const { execFileSync } = require('node:child_process')

const gen = path.join(__dirname, 'gen-filelist.mjs')

function tree(withHash) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-gen-'))
  const base = withHash ? path.join(root, '#repo') : root
  const payload = path.join(base, 'build', 'payload')
  fs.mkdirSync(path.join(payload, 'resources', 'app', 'ui'), { recursive: true })
  fs.writeFileSync(path.join(payload, 'PsychoVault.exe'), 'x')
  fs.writeFileSync(path.join(payload, 'resources', 'app', 'main.js'), 'yy')
  fs.writeFileSync(path.join(payload, 'resources', 'app', 'ui', 'index.html'), 'zzz')
  return { base, payload }
}

test('one SetOutPath and one File per real file, with relative backslashed paths', () => {
  const { base, payload } = tree(true)
  const out = path.join(base, 'build', 'payload-files.nsh')
  execFileSync(process.execPath, [gen, payload, out, base], { stdio: 'pipe' })
  const text = fs.readFileSync(out, 'utf8')

  assert.ok(!/File \/r/.test(text), 'the output still asks NSIS to enumerate a folder')
  assert.match(text, /^SetOutPath "\$INSTDIR"/m)
  assert.match(text, /^File "build\\payload\\PsychoVault\.exe"$/m)
  assert.match(text, /^SetOutPath "\$INSTDIR\\resources\\app\\ui"$/m)
  assert.match(text, /^File "build\\payload\\resources\\app\\ui\\index\.html"$/m)
  // 3 files
  assert.strictEqual(text.split('\n').filter((l) => l.trim().startsWith('File "')).length, 3)
})

test('a parent folder is created before anything is written into it', () => {
  const { base, payload } = tree(false)
  const out = path.join(base, 'list.nsh')
  execFileSync(process.execPath, [gen, payload, out, base], { stdio: 'pipe' })
  const lines = fs.readFileSync(out, 'utf8').split('\n').map((l) => l.trim())
  const appDir = lines.indexOf('SetOutPath "$INSTDIR\\resources\\app"')
  const appFile = lines.indexOf('File "build\\payload\\resources\\app\\main.js"')
  assert.ok(appDir !== -1 && appFile > appDir, 'the file is written before its folder is set')
})

test('an empty payload fails loudly instead of writing an empty installer', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-gen-empty-'))
  fs.mkdirSync(path.join(root, 'payload'))
  assert.throws(() => {
    execFileSync(process.execPath, [gen, path.join(root, 'payload'), path.join(root, 'o.nsh'), root], { stdio: 'pipe' })
  })
})

test('the build step generates the list and hands it to makensis', () => {
  const bat = fs.readFileSync(path.join(__dirname, '..', 'PsychoVault_easy_build.bat'), 'utf8')
  assert.match(bat, /gen-filelist\.mjs" "%PAYLOAD%" "%BUILD%\\payload-files\.nsh"/)
  assert.match(bat, /\/DFILELIST=build\\payload-files\.nsh/)
  const nsi = fs.readFileSync(path.join(__dirname, '..', 'installer.nsi'), 'utf8')
  assert.ok(!/File \/r "\$\{PAYLOADDIR\}/.test(nsi), 'installer.nsi still uses File /r on the payload')
  assert.match(nsi, /!include "\$\{FILELIST\}"/)
})

test('Linux installer build generates and passes a file list for the staged payload', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-linux-build-'))
  const payload = path.join(root, 'payload')
  const stage = path.join(root, 'stage')
  const bin = path.join(root, 'bin')
  fs.mkdirSync(payload)
  fs.mkdirSync(bin)
  fs.writeFileSync(path.join(payload, 'PsychoVault.exe'), 'runtime')
  const makensis = `#!/usr/bin/env node
const fs = require('fs'), path = require('path');
const args = process.argv.slice(2);
const find = name => (args.find(a => a.startsWith('-D' + name + '=')) || '').slice(name.length + 3);
const list = find('FILELIST'), out = find('OUTFILE'), stage = find('PAYLOADDIR');
if (!list || !fs.existsSync(list)) throw Error('FILELIST is missing');
const lines = fs.readFileSync(list, 'utf8').split(/\\r?\\n/);
const files = lines.filter(line => line.startsWith('File "')).map(line => line.slice(6, -1).replace(/\\\\/g, '/'));
if (!files.some(f => f.endsWith('/PsychoVault.exe')) || !files.some(f => f.endsWith('/resources/app/main.js'))) throw Error('payload missing from file list');
for (const file of files) if (!fs.existsSync(path.resolve(file))) throw Error('missing generated file: ' + file);
fs.writeFileSync(out, 'fake installer');
fs.writeFileSync(process.env.LIST_RECORD, list);
`;
  const sevenzip = `#!/usr/bin/env node
const fs = require('fs'), path = require('path');
const out = process.argv.find(a => a.startsWith('-o')).slice(2);
fs.cpSync(process.env.STAGE, out, { recursive: true });
`;
  fs.writeFileSync(path.join(bin, 'makensis'), makensis, { mode: 0o755 })
  fs.writeFileSync(path.join(bin, '7z'), sevenzip, { mode: 0o755 })
  try {
    const record = path.join(root, 'list-path')
    const output = execFileSync('bash', [path.join(__dirname, 'build-installer.sh')], {
      cwd: path.join(__dirname, '..'),
      env: { ...process.env, PATH: bin + path.delimiter + process.env.PATH, PAYLOAD: payload,
        STAGE: stage, OUT: path.join(root, 'setup.exe'), NSIS_TMPDIR: root, LIST_RECORD: record },
      encoding: 'utf8'
    })
    assert.match(output, /file list: \d+ files/)
    assert.match(output, /done\./)
    assert.ok(!fs.existsSync(fs.readFileSync(record, 'utf8')), 'temporary include was not cleaned up')
  } finally {
    fs.rmSync(root, { recursive: true, force: true })
  }
})
