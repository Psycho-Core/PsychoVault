'use strict'
// Static checks for PsychoVault_easy_build.bat.
//
// The batch file cannot be executed on the CI runner, so this verifies the
// things that actually break a .bat before anyone runs it: the file encoding
// and line endings cmd expects, labels that resolve, balanced quoting and
// parentheses, destructive commands that point at the wrong directory, the
// repo paths it references, and the NSIS contract between the script and
// installer.nsi. A failure here is a real defect, not a style opinion.

const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const ROOT = path.join(__dirname, '..')
const BAT_PATH = path.join(ROOT, 'PsychoVault_easy_build.bat')
const raw = fs.readFileSync(BAT_PATH, 'latin1')

// cmd is happiest with CRLF and hostile to anything outside the ANSI range.
const lines = raw.split('\r\n')
const text = lines.join('\n')

// Strip a line's rem/:: comment tail for the parser checks. Quoted text is
// dropped too, so `echo "see (this)"` cannot be mistaken for a block opener.
function code(line) {
  return line.replace(/"(?:[^"]*)"/g, '""').replace(/^\s*rem\b.*$/i, '')
}

test('encoding: ASCII only', () => {
  const bad = [...text].filter((c) => c.charCodeAt(0) > 126)
  assert.deepStrictEqual(bad, [], `non-ASCII characters: ${JSON.stringify([...new Set(bad)])}`)
})

test('line endings: every line ends CRLF, no lone LF', () => {
  assert.ok(raw.endsWith('\r\n'), 'file must end with CRLF')
  const loneLf = raw.split('\r\n').filter((l) => l.includes('\n'))
  assert.deepStrictEqual(loneLf, [], 'found a bare LF - cmd can mis-parse multi-line blocks')
  assert.ok(!raw.includes('\r\r'), 'found a stray CR')
})

test('a first line that is @echo off, so the script is silent off the top', () => {
  assert.match(lines[0], /^@echo off$/i)
})

test('delayed expansion is enabled before the first !VAR! use', () => {
  const enable = lines.findIndex((l) => /setlocal/i.test(l) && /enabledelayedexpansion/i.test(l))
  const firstUse = lines.findIndex((l) => /![A-Za-z_]\w*!/.test(l))
  assert.ok(enable !== -1, 'setlocal EnableDelayedExpansion is missing')
  assert.ok(firstUse === -1 || enable < firstUse, '!VAR! used before delayed expansion is on')
})

test('every goto resolves to a label, and every label is reachable', () => {
  const gotos = new Set()
  for (const line of lines) {
    for (const m of line.matchAll(/goto\s+:(\w+)/gi)) gotos.add(m[1].toLowerCase())
  }
  const labels = new Set()
  for (const line of lines) {
    const m = /^:(\w+)\s*$/.exec(line.trim())
    if (m) labels.add(m[1].toLowerCase())
  }
  const missing = [...gotos].filter((g) => !labels.has(g))
  assert.deepStrictEqual(missing, [], `goto without a label: ${missing.join(', ')}`)
  const unused = [...labels].filter((l) => !gotos.has(l))
  assert.deepStrictEqual(unused, [], `label nothing jumps to: ${unused.join(', ')}`)
})

test('no duplicate labels', () => {
  const seen = new Map()
  lines.forEach((line, i) => {
    const m = /^:(\w+)\s*$/.exec(line.trim())
    if (!m) return
    const key = m[1].toLowerCase()
    assert.ok(!seen.has(key), `duplicate label :${m[1]} on lines ${seen.get(key) + 1} and ${i + 1}`)
    seen.set(key, i)
  })
})

test('double quotes are balanced on every line', () => {
  const bad = []
  lines.forEach((line, i) => {
    const n = (code(line).match(/"/g) || []).length
    if (n % 2 !== 0) bad.push(`line ${i + 1}: ${line.trim()}`)
  })
  assert.deepStrictEqual(bad, [], `odd number of quotes:\n${bad.join('\n')}`)
})

// A `(` inside a block that is not the block itself breaks cmd. Track depth and
// require the closing paren to be alone on its line, which is how this file is
// written throughout.
test('parenthesised blocks open and close cleanly', () => {
  let depth = 0
  const problems = []
  lines.forEach((line, i) => {
    const c = code(line).trim()
    if (!c) return
    const opens = (c.match(/\(/g) || []).length
    const closes = (c.match(/\)/g) || []).length
    if (depth > 0 && c !== ')' && (opens !== closes)) {
      problems.push(`line ${i + 1} inside a block has unbalanced parens: ${line.trim()}`)
    }
    depth += opens - closes
    if (depth < 0) problems.push(`line ${i + 1} closes a block that was never opened`)
  })
  assert.deepStrictEqual(problems, [], problems.join('\n'))
  assert.strictEqual(depth, 0, 'a block is still open at the end of the file')
})

test('no destructive command points at the repo root or a drive root', () => {
  const bad = []
  lines.forEach((line, i) => {
    const t = line.trim()
    if (!/^(del|rmdir|rd|erase)\b/i.test(t)) return
    const targetsRoot = /"%(ROOT|BUILD)%\\?"|"%ROOT%"|%SystemDrive%\\?"/i.test(t)
    const wildcardRoot = /"%~?[A-Za-z:]*\\?\*\.?\*?"$/i.test(t) && !/%PAYLOAD%|%BUILD%/i.test(t)
    if (targetsRoot || wildcardRoot) bad.push(`line ${i + 1}: ${t}`)
  })
  assert.deepStrictEqual(bad, [], `destructive command with too wide a target:\n${bad.join('\n')}`)
})

test('the build steps the guide promises are all present', () => {
  const required = [
    ['node presence check', /where node\b/i],
    ['electron install', /npm install\b/i],
    ['runtime staging', /robocopy .*electron\\dist/i],
    ['exe rebrand', /ren .*electron\.exe"?\s+PsychoVault\.exe/i],
    ['icon and version stamp', /tools\\patch-exe\.mjs/i],
    ['app source copy', /robocopy .*"%ROOT%\\app"/i],
    ['documents for the installer', /README\.md"\s+"%PAYLOAD%\\README\.txt"/i],
    ['NSIS lookup', /where makensis/i],
    ['NSIS run', /installer\.nsi"?\s*$/m],
    ['portable fallback when NSIS is absent', /if not defined MAKENSIS goto/i]
  ]
  const missing = required.filter(([, re]) => !re.test(text)).map(([name]) => name)
  assert.deepStrictEqual(missing, [], `missing step: ${missing.join(', ')}`)
})

test('the app copy excludes node_modules and the dead wireproxy binary', () => {
  const copy = lines.find((l) => /robocopy .*"%ROOT%\\app"\s+"%PAYLOAD%\\resources\\app"/i.test(l))
  assert.ok(copy, 'no robocopy of app\\ into resources\\app')
  assert.match(copy, /\/XD\s+node_modules/i, 'node_modules would be packaged into the browser')
  assert.match(copy, /\/XD\s+[^\r\n]*\btools\b/i, 'the 10 MB unused app\\tools\\wireproxy.exe is packaged')
})

test('step labels are sequential and agree on the total', () => {
  const marks = [...text.matchAll(/\[(\d+)\/(\d+)\]/g)].map((m) => [Number(m[1]), Number(m[2])])
  assert.ok(marks.length >= 6, 'step labels are missing')
  const totals = new Set(marks.map(([, total]) => total))
  assert.strictEqual(totals.size, 1, `inconsistent step totals: ${[...totals].join(', ')}`)
  const total = [...totals][0]
  const steps = marks.map(([n]) => n)
  assert.deepStrictEqual(steps, steps.map((_, i) => i + 1), `steps out of order: ${steps.join(',')}`)
  assert.strictEqual(steps.length, total, `${steps.length} steps but the labels say /${total}`)
})

test('the payload is verified before anything is packaged', () => {
  assert.match(text, /for %%. in \(%VERIFY%\) do if not exist/i, 'no existence check over the verify list')
  assert.match(text, /%%~zf EQU 0/i, 'no empty-file check')
  const verifyLine = lines.find((l) => /^set "VERIFY=PsychoVault\.exe/i.test(l))
  assert.ok(verifyLine, 'verify list is not defined')
  for (const file of ['main.js', 'preload.js', 'auto-updater.js', 'update-public-key.pem', 'ui\\renderer.js']) {
    assert.ok(text.includes(file), `verify list does not cover ${file}`)
  }
  const verifyAt = lines.findIndex((l) => /\[\d+\/\d+\] Checking the staged payload/i.test(l))
  const nsisAt = lines.findIndex((l) => /installer\.nsi"?\s*$/m.test(l))
  assert.ok(verifyAt > 0 && nsisAt > verifyAt, 'verification must run before makensis, not after')
})

test('release notes are written with both hashes and the change list', () => {
  assert.match(text, /Release-Notes-%VERSION%\.txt/)
  assert.match(text, /certutil -hashfile/i, 'no SHA-256 of the built exe')
  assert.match(text, /log -10 --oneline/i, 'no commit list')
  for (const heading of ['WHAT IS IN THIS BUILD', 'WHAT CHANGED', 'HOW TO USE IT', 'NOTES']) {
    assert.ok(text.includes(heading), `release notes are missing the ${heading} section`)
  }
  const notesAt = lines.findIndex((l) => /Writing release notes/i.test(l))
  const nsisAt = lines.findIndex((l) => /installer\.nsi"?\s*$/m.test(l))
  assert.ok(notesAt > nsisAt, 'notes must be written after the installer so its hash can be included')
})

test('a token in app\\update-token.json is checked against .gitignore', () => {
  assert.match(text, /findstr \/c:"update-token\.json"/i)
  assert.match(text, /update-token\.json" "%ROOT%\\\.gitignore"/i)
})

test('every %ROOT%-relative path the script touches exists in the repo', () => {
  const refs = new Set()
  for (const m of text.matchAll(/%ROOT%\\([A-Za-z0-9_.\-\\]+)/g)) refs.add(m[1])
  const missing = []
  for (const ref of refs) {
    const rel = ref.replace(/\\+$/, '')
    if (!rel) continue
    if (/%/.test(rel)) continue
    const abs = path.join(ROOT, ...rel.split('\\').filter(Boolean))
    if (fs.existsSync(abs)) continue
    // outputs and copies the script creates itself are expected to be absent
    if (/^(build|PsychoVault-Setup-)/i.test(rel)) continue
    if (/^app\\node_modules\\|^app\\update-token\.json$/i.test(rel)) continue
    if (/\.(md|txt)$/i.test(rel) || /^assets\\psychovault\.ico$|^installer\.nsi$|^tools\\|^app\\/i.test(rel)) {
      missing.push(rel)
    }
  }
  assert.deepStrictEqual(missing, [], `referenced but not in the repo: ${missing.join(', ')}`)
})

test('NSIS is given every define installer.nsi requires', () => {
  const nsi = fs.readFileSync(path.join(ROOT, 'installer.nsi'), 'latin1')
  const needed = [...nsi.matchAll(/!ifndef\s+([A-Z_]+)/g)].map((m) => m[1])
  // !define MUI_ICON ... at the top has no !ifndef guard, but the build script
  // passes it anyway; VERSION/PAYLOADDIR/OUTFILE are guarded.
  assert.ok(needed.length >= 3, 'installer.nsi changed shape - re-check the defines')
  const missing = needed.filter((name) => !new RegExp(`/D${name}=`, 'i').test(text))
  assert.deepStrictEqual(missing, [], `NSIS define never passed: ${missing.join(', ')}`)
})

test('payload files that installer.nsi reads at build time are created', () => {
  const nsi = fs.readFileSync(path.join(ROOT, 'installer.nsi'), 'latin1')
  const refs = [...nsi.matchAll(/\$\{PAYLOADDIR\}[\\/]([A-Za-z0-9_.\-]+)/g)].map((m) => m[1])
  assert.ok(refs.length > 0, 'installer.nsi references no payload files - re-check')
  for (const file of refs) {
    assert.ok(
      new RegExp(`%PAYLOAD%\\\\${file.replace(/\./g, '\\.')}"`, 'i').test(text),
      `installer.nsi needs ${file} in the payload but the script never writes it`
    )
  }
})

test('the payload exe the installer packages is the one that gets patched', () => {
  const patchLine = lines.find((l) => /patch-exe\.mjs/i.test(l))
  assert.ok(patchLine, 'no patch-exe call')
  assert.match(patchLine, /"%PAYLOAD%\\PsychoVault\.exe"/i)
  assert.match(patchLine, /%ROOT%\\assets\\psychovault\.ico/i)
  assert.match(patchLine, /%VERSION%/, 'patch-exe is not given the version')
})

test('no literal ! inside echoed text (delayed expansion would swallow it)', () => {
  const bad = []
  lines.forEach((line, i) => {
    const t = line.trim()
    if (!/^echo\b/i.test(t)) return
    if (/![A-Za-z_]\w*!/.test(t)) return // purposeful expansion
    if (t.includes('!')) bad.push(`line ${i + 1}: ${t}`)
  })
  assert.deepStrictEqual(bad, [], `bare ! in echo:\n${bad.join('\n')}`)
})

test('it pauses before closing, so a double-click user can read the result', () => {
  assert.match(text, /\bpause\b/i)
})

test('no code path falls off the end without exiting', () => {
  assert.match(text, /exit \/b 0/i)
})

test('the SHA-256 capture keeps the first certutil line and ignores the status line', () => {
  // "if not defined X" is the whole mechanism: seeding X with a placeholder in
  // front of the loop silently turns the hash into that placeholder.
  assert.ok(!/set "EXEHASH=unknown"/.test(text), 'EXEHASH is seeded, so the real hash is never kept')
  assert.ok(!/set "SETUPHASH=unknown"/.test(text), 'SETUPHASH is seeded, so the real hash is never kept')
  assert.match(text, /set "EXEHASH="\r?\n/)
  assert.match(text, /do if not defined EXEHASH set "EXEHASH=%%h"/)
  assert.match(text, /do if not defined SETUPHASH set "SETUPHASH=%%h"/)
  assert.match(text, /if defined EXEHASH goto :notes_setup_hash/, 'no fallback when the hash cannot be read')
})

test('the version is validated before it reaches patch-exe and NSIS', () => {
  const guard = lines.find((l) => /findstr \/r \/c:"\^\[0-9\]/.test(l))
  assert.ok(guard, 'no three-part version check')
  assert.match(guard, /\$/, 'the version pattern is not anchored at both ends')
  const after = lines[lines.indexOf(guard) + 1]
  assert.match(after, /goto :fail_version/i)
})

test('a folder path batch cannot handle is refused with an explanation', () => {
  assert.match(text, /findstr \/c:"\(" \/c:"&"/, 'no bracket or ampersand check on the repo path')
  assert.match(text, /goto :bad_path/i)
  assert.match(text, /:bad_path/)
})

test('percent signs in note text are doubled, or cmd eats them', () => {
  const noteLines = lines.filter((l) => /echo   %%APPDATA%%|%APPDATA%/.test(l))
  assert.ok(noteLines.length > 0, 'the profile path is not mentioned in the notes')
  for (const line of noteLines) {
    assert.ok(!/(?<!%)%APPDATA%/.test(line), `single %%APPDATA%% will not survive cmd: ${line.trim()}`)
  }
})

test('a missing runtime is retried three ways before the build is given up', () => {
  // Field report: npm printed "up to date in 634ms" while dist\electron.exe was absent,
  // because the package folder existed but the post-install download had never finished.
  // "npm install --force" does not fix that; re-running the downloader does.
  assert.match(text, /node "node_modules.{1,2}electron.{1,2}install\.js"/, 'the downloader is never re-run')
  assert.match(text, /npm rebuild electron/, 'the package is never rebuilt')
  assert.match(text, /rmdir \/s \/q "%ROOT%.{1,2}app.{1,2}node_modules.{1,2}electron"/, 'the half-install is never removed')

  const retries = [
    lines.findIndex((l) => /node "node_modules.{1,2}electron.{1,2}install\.js"/.test(l)),
    lines.findIndex((l) => /npm rebuild electron/.test(l)),
    lines.findIndex((l) => /rmdir \/s \/q "%ROOT%.{1,2}app.{1,2}node_modules.{1,2}electron"/.test(l))
  ]
  const giveUp = lines.findIndex((l) => /^:no_electron/.test(l))
  const jump = lines.findIndex((l) => /goto :no_electron/.test(l))
  assert.ok(jump > Math.max(...retries), 'it gives up before trying the retries')
  assert.ok(giveUp > 0)
})

test('the runtime failure names the three causes and how to test each', () => {
  const block = lines.slice(lines.findIndex((l) => /^:no_electron/.test(l))).join('\n')
  assert.match(block, /npm config get ignore-scripts/, 'no check for skipped install scripts')
  assert.match(block, /ELECTRON_MIRROR/, 'no mirror fallback offered')
  assert.match(block, /npmmirror\.com/, 'the mirror hint has no working URL')
  assert.match(block, /curl -I https:\/\/github\.com\/electron/, 'no way to test whether the download host is reachable')
})

test('a missing exe with a present dist\\version is treated as a deletion, not a download failure', () => {
  // dist\version is written at the end of a successful unpack, so version-without-exe means
  // the files were extracted and electron.exe then vanished - in practice quarantine.
  assert.match(text, /if not exist "%ROOT%.{1,2}app.{1,2}node_modules.{1,2}electron.{1,2}dist.{1,2}version" goto :runtime_rebuild/)
  const reExtract = lines.findIndex((line) => /rmdir \/s \/q "%ROOT%.{1,2}app.{1,2}node_modules.{1,2}electron.{1,2}dist"/.test(line))
  assert.ok(reExtract > 0, 'the stale dist is never cleared before re-extracting')
  assert.ok(lines.some((line) => /electron\.exe is still not there after re-extracting/.test(line)))
  assert.ok(lines.some((line) => /^:runtime_rebuild/.test(line)), 'the rebuild branch label is missing')
})

test('the antivirus case is named in the failure text with a way to fix it', () => {
  const block = lines.slice(lines.findIndex((line) => /^:no_electron/.test(line))).join('\n')
  assert.match(block, /Protection history/, 'no pointer to the quarantine history')
  assert.match(block, /Add-MpPreference -ExclusionPath/, 'no exclusion command')
  assert.match(block, /electron\.exe.*quarantine|quarantine.*electron\.exe/s, 'quarantine is not connected to the missing exe')
  assert.match(block, /electron.{1,2}Cache/, 'the download cache location is not mentioned')
})

test('a payload is checked for a whole runtime, not just the exe', () => {
  // Field report: dist held electron.exe and nothing else (no DLLs, no locales, no
  // resources, no version file) and the old check passed it, producing a browser that
  // cannot start. A complete win32-x64 runtime has all of the below.
  const runtimeLine = lines.find((l) => /^set "RUNTIME=/.test(l))
  assert.ok(runtimeLine, 'no runtime completeness list')
  for (const part of ['icudtl.dat', 'resources.pak', 'snapshot_blob.bin', 'ffmpeg.dll', 'locales', 'resources']) {
    assert.ok(text.includes(part), `the runtime list does not require ${part}`)
  }
})

test('the missing-file failure names the files', () => {
  assert.match(text, /set "MISSING="\r?\n/)
  assert.match(text, /set "MISSING=!MISSING! %%f"/)
  assert.match(text, /if defined MISSING goto :fail_verify/)
  const block = lines.slice(lines.findIndex((l) => /^:fail_verify/.test(l))).join('\n')
  assert.match(block, /echo      %MISSING%/, 'the failure does not print which files are missing')
})

test('only a payload that cannot start is fatal; the rest warns and carries on', () => {
  // Field report: a complete, working payload was refused by this check, costing the user
  // a build. Only HARD failures stop it now.
  assert.match(text, /set "HARD=PsychoVault\.exe/)
  assert.match(text, /for %%. in \(%HARD%\) do if not exist .* set "MISSING=!/)
  assert.match(text, /if defined MISSING goto :fail_verify/)

  // the advisory list is printed but does not stop the build
  assert.match(text, /set "WARNED="/)
  assert.match(text, /if defined WARNED echo       WARNING: missing or empty, continuing anyway/)
  assert.ok(!/if defined WARNED goto/.test(text), 'the advisory list still stops the build')

  // a stale default_app.asar is removed rather than fatal
  assert.match(text, /del \/f \/q "%PAYLOAD%.{1,2}resources.{1,2}default_app\.asar"/)
})

test('the empty-file probe measures the file inside the payload, not a bare name', () => {
  // Field report: every file was reported "(empty)" including PsychoVault.exe, because
  // "if %%~zf EQU 0" was asked about a name relative to the repo root, which does not exist -
  // and an empty value compares equal to 0. The probe must run from inside the payload.
  const block = text.slice(text.indexOf('set "WARNED="'), text.indexOf('echo       Payload complete'))
  const pushdAt = block.indexOf('pushd "%PAYLOAD%"')
  const probeAt = block.indexOf('%%~zf EQU 0')
  const popdAt = block.indexOf('popd')
  assert.ok(pushdAt !== -1, 'the probe does not change into the payload')
  assert.ok(probeAt > pushdAt && probeAt < popdAt, 'the size probe runs outside the payload directory')
})

test('the payload is counted again immediately before NSIS runs', () => {
  // A scanner can remove the staged files between the check and makensis, which then reports
  // "no files found" while the check said everything was present.
  const nsisAt = lines.findIndex((l) => /installer\.nsi"?\s*$/m.test(l))
  const countAt = lines.findIndex((l) => /PAYLOADCOUNT/.test(l))
  assert.ok(countAt > 0 && countAt < nsisAt, 'there is no recount before makensis')
  assert.match(text, /if "%PAYLOADCOUNT%"=="0" goto :fail_vanished/)
  assert.match(text, /^:fail_vanished$/m)
})

test('a vanished payload is blamed on quarantine, with the exclusion command', () => {
  const block = lines.slice(lines.findIndex((l) => /^:fail_vanished$/.test(l))).join('\n')
  assert.match(block, /Add-MpPreference -ExclusionPath/)
  assert.match(block, /Protection history/)
})

test('an NSIS failure still writes the release notes', () => {
  const block = lines.slice(lines.findIndex((l) => /^:fail_nsis$/.test(l))).join('\n')
  assert.match(block, /goto :notes/, 'the notes step is skipped when the installer fails')
})

test('makensis is never handed a path that contains #', () => {
  // Field report: the payload was counted as 20 files, then NSIS said "no files found" for
  // the same folder. The repo path contains "#" and NSIS reads # as a comment marker.
  const calls = lines.filter((l) => /MAKENSIS.*installer\.nsi/.test(l) && !/^rem/.test(l.trim()));
  assert.ok(calls.length >= 2, 'the installer step no longer has a relative attempt and a fallback');
  for (const call of calls) {
    assert.ok(!/%ROOT%|%PAYLOAD%|%SETUP%/.test(call), `makensis is given an absolute path: ${call.trim()}`)
    assert.match(call, /\/DPAYLOADDIR=(build\\payload|payload)/, `unexpected payload define: ${call.trim()}`)
  }
  assert.match(text, /pushd "%ROOT%"/, 'the relative attempt does not run from the repo root')
})

test('the fallback copy is staged somewhere without #', () => {
  const at = lines.findIndex((l) => /:nsis_try_copy/.test(l));
  assert.ok(at > 0, 'there is no fallback path for an NSIS build that cannot see the payload')
  const block = lines.slice(at, at + 24).join('\n');
  assert.match(block, /set "STAGE=%TEMP%\\PsychoVault-stage"/);
  assert.match(block, /findstr \/c:"#"/, 'the stage path is not checked for #');
  assert.match(block, /set "STAGE=C:\\PsychoVault-stage"/, 'there is no hash-free fallback directory');
})
