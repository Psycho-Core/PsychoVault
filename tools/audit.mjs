#!/usr/bin/env node
// PsychoVault self-audit. Checks the things a syntax check cannot: that every IPC
// channel a bridge calls is actually registered, that every setting has a control and
// every control writes a setting the app reads, that no element id is referenced but
// missing, and that queries are scoped so one editor cannot eat another's rows.
// Run: node tools/audit.mjs    (exit 1 = defects found)
import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'

const ROOT = path.resolve(import.meta.dirname, '..')
const A = (rel) => path.join(ROOT, 'app', rel)
const read = (rel) => fs.readFileSync(A(rel), 'utf8')

const main = read('main.js')
const preload = read('preload.js')
const pagePreload = read('page-preload.js')
const chromeHtml = read('ui/index.html')
const chromeJs = read('ui/renderer.js')
const settingsHtml = read('ui/settings.html')
const newtabHtml = read('ui/newtab.html')
const downloadSave = read('download-save.js')
const css = read('ui/style.css')

const defects = []
const notes = []
const uniq = (a) => [...new Set(a)].sort()

// ---------------------------------------------------------------- 1. ipc parity
const handlers = uniq([...main.matchAll(/ipcMain\.handle\(\s*'([^']+)'/g)].map((m) => m[1]))
const bridgeCalls = uniq([
  ...[...preload.matchAll(/invoke\(\s*'([^']+)'/g)].map((m) => m[1]),
  ...[...pagePreload.matchAll(/invoke\(\s*'([^']+)'/g)].map((m) => m[1])
])
const sendEvents = uniq([
  ...[...main.matchAll(/sendChrome\(\s*'([^']+)'/g)].map((m) => m[1]),
  ...[...main.matchAll(/broadcast\(\s*'([^']+)'/g)].map((m) => m[1])
])
const allowed = (preload.match(/const allowed = \[([^\]]+)\]/) || [, ''])[1]
  .split(',').map((x) => x.trim().replace(/['"]/g, '')).filter(Boolean)

for (const ch of bridgeCalls) if (!handlers.includes(ch)) defects.push(`bridge invokes "${ch}" but no ipcMain.handle registers it`)
for (const ch of sendEvents) if (!allowed.includes(ch)) defects.push(`main pushes "${ch}" but preload's allow-list drops it`)
const unused = handlers.filter((h) => !bridgeCalls.includes(h))
if (unused.length) notes.push(`handlers not called from a bridge (reachable another way, or dead): ${unused.join(', ')}`)

// ---------------------------------------------------------------- 2. settings
const defBlock = main.match(/const DEFAULT_SETTINGS = \{([\s\S]*?)\n\}/)[1]
const keys = uniq([...defBlock.matchAll(/^\s{2}([a-zA-Z][a-zA-Z0-9]*)\s*:/gm)].map((m) => m[1]))
const reads = uniq([...main.matchAll(/settings\.([a-zA-Z][a-zA-Z0-9]*)/g)].map((m) => m[1]))
  .filter((k) => !['length', 'map', 'forEach', 'filter', 'json', 'html', 'md'].includes(k))
const declared = uniq([...keys, ...reads])
const uiIds = uniq([...settingsHtml.matchAll(/id="([a-zA-Z0-9_]+)"/g)].map((m) => m[1]))

const ALIAS = { speedDial: 'tiles', services: 'svcList', containers: 'contList' }
// Keys the app writes for itself: schema markers, extension inventory, and the retired
// askSaveLocation preference (consumed only by the one-time migration to mandatory Save As).
const INTERNAL = ['settingsVersion', 'extensions', 'askSaveLocation']

// The settings view must have one navigation button for every category panel, and all
// real sections must live inside one of those panels.
const navCategories = [...settingsHtml.matchAll(/data-category="([a-z0-9-]+)"/g)].map((m) => m[1])
const panels = [...settingsHtml.matchAll(/data-category-panel="([a-z0-9-]+)"[^>]*>/g)]
const panelCategories = panels.map((m) => m[1])
if (!navCategories.length || new Set(navCategories).size !== navCategories.length) defects.push('settings categories: missing or duplicate navigation category')
if (navCategories.length !== panelCategories.length || navCategories.some((x) => !panelCategories.includes(x))) defects.push('settings categories: navigation buttons and panels do not match')
let groupedSections = 0
for (let i = 0; i < panels.length; i++) {
  const from = panels[i].index + panels[i][0].length
  const to = i + 1 < panels.length ? panels[i + 1].index : settingsHtml.length
  groupedSections += (settingsHtml.slice(from, to).match(/<section>/g) || []).length
}
const allSections = (settingsHtml.match(/<section>/g) || []).length
if (!panels.length || groupedSections !== allSections) defects.push(`settings categories: only ${groupedSections} of ${allSections} sections are inside a category`)
if (/id="askSaveLocation"/.test(settingsHtml)) defects.push('downloads: save-location prompt must not be disableable in Settings')
const downloadFlow = main.slice(main.indexOf('function watchDownloads(ses)'), main.indexOf('function broadcast(ch, payload)', main.indexOf('function watchDownloads(ses)')))
if (!downloadFlow.includes('requestDownloadSavePath') || !downloadFlow.includes('item.resume()')) defects.push('downloads: every download must wait for its Save As path before resuming')
if (downloadFlow.indexOf('item.pause()') < 0 || downloadFlow.indexOf('item.pause()') > downloadFlow.indexOf('requestDownloadSavePath')) defects.push('downloads: pause the item before doing save-dialog setup')
if (downloadFlow.includes('showSaveDialogSync')) defects.push('downloads: the Save As picker must not block synchronously inside will-download')
if (!downloadSave.includes('item.pause()') || !downloadSave.includes('dialog.showSaveDialog') || !downloadSave.includes('item.setSavePath') || !downloadSave.includes('item.cancel()')) defects.push('downloads: pause, async Save As, selected path, and cancellation must all be implemented')
if (downloadSave.indexOf('item.pause()') > downloadSave.indexOf('dialog.showSaveDialog')) defects.push('downloads: the transfer must be paused before the Save As dialog opens')
if (downloadFlow.includes('settings.askSaveLocation')) defects.push('downloads: legacy setting still controls whether Save As appears')

// A BrowserWindow maximize event must never publish a partial state snapshot that
// replaces the tab list in the chrome renderer. Pinned tabs should use the normal tab
// width rules, not an icon-only fixed width.
if (/sendChrome\(['"]state['"]\s*,\s*\{\s*maximized\s*:/.test(main)) defects.push('window state: maximize/unmaximize pushes a partial snapshot instead of calling sendState()')
if (!/const s = state = \{ \.\.\.state, \.\.\.update \}/.test(chromeJs)) defects.push('window state: renderer must merge state updates so a partial update cannot erase tabs')
if (!/maximized:\s*this\.win\.isMaximized\(\)/.test(main)) defects.push('window state: full state snapshot does not include the current maximize state')
const pinnedRule = css.match(/\.tab\.pinned\s*\{([^}]*)\}/)
if (pinnedRule && /(?:^|[;\s])(?:width|min-width|max-width)\s*:|(?:^|[;\s])flex\s*:\s*0\s+0\s+\d/.test(pinnedRule[1])) defects.push('pinned tabs: a fixed narrow width overrides the normal tab size')

for (const k of declared) {
  if (INTERNAL.includes(k)) continue
  const want = ALIAS[k] || k
  if (!uiIds.includes(want)) defects.push(`setting "${k}" has no control (expected id="${want}") in the settings page`)
}
for (const id of uiIds) {
  if (!declared.includes(id)) {
    // ids that are pure UI (buttons, panels) are fine; a control whose name matches a
    // setting must be a setting
    if (/^(btn|tab|omni|panel|find|palette|rail|drag|ws|toast|win|protonVpnDownload|downloadDirPick)/.test(id)) continue
    if (/^(v|e|data|tile|svc|cont|mod|flag|restart|relaunch|clear|search|spell)/.test(id)) continue
    notes.push(`settings control "${id}" does not map to a known key`)
  }
}

// ---------------------------------------------------------------- 3. element ids
function checkIds(htmlSources, jsSource, label) {
  const present = new Set()
  for (const h of htmlSources) for (const m of h.matchAll(/id="([a-zA-Z0-9_-]+)"/g)) present.add(m[1])
  for (const m of jsSource.matchAll(/\.id = '([a-zA-Z0-9_-]+)'/g)) present.add(m[1])
  const dup = []
  for (const h of htmlSources) {
    const seen = new Map()
    for (const m of h.matchAll(/id="([a-zA-Z0-9_-]+)"/g)) {
      seen.set(m[1], (seen.get(m[1]) || 0) + 1)
      if (seen.get(m[1]) === 2) dup.push(m[1])
    }
  }
  for (const d of uniq(dup)) defects.push(`${label}: duplicate id="${d}" - the second one is unreachable`)

  const refs = new Set()
  for (const m of jsSource.matchAll(/\$\('#([a-zA-Z0-9_-]+)'\)/g)) refs.add(m[1])
  for (const m of jsSource.matchAll(/getElementById\('([a-zA-Z0-9_-]+)'\)/g)) refs.add(m[1])
  for (const m of jsSource.matchAll(/\bs\('([a-zA-Z0-9_-]+)'\)/g)) refs.add(m[1])
  for (const r of refs) if (!present.has(r)) defects.push(`${label}: JS looks up #${r} but no element carries that id`)

  // classes referenced from JS must exist somewhere in the CSS or be created by JS
  const jsClasses = new Set()
  for (const m of jsSource.matchAll(/className = '([a-z-]+)/g)) jsClasses.add(m[1])
  for (const m of jsSource.matchAll(/classList\.(?:add|toggle|contains)\('([a-z-]+)'/g)) jsClasses.add(m[1])
  const cssText = css + '\n' + htmlSources.join('\n')
  for (const c of jsClasses) {
    if (!c) continue
    if (!cssText.includes('.' + c) && !cssText.includes('#' + c)) notes.push(`${label}: class ".${c}" is set by JS but never styled`)
  }
}
checkIds([chromeHtml], chromeJs, 'chrome')
checkIds([settingsHtml, newtabHtml], read('ui/settings.html').match(/<script>([\s\S]*)<\/script>/)[1], 'settings')

// ------------------------------------------------- 4. scoped DOM queries (bug class)
for (const [label, src] of [['renderer.js', chromeJs], ['settings.html', settingsHtml]]) {
  for (const m of src.matchAll(/document\.querySelectorAll\('\.([a-z-]+)'\)/g)) {
    if (['tile-row', 'tab', 'row'].includes(m[1])) {
      defects.push(`${label}: document.querySelectorAll('.${m[1]}') is not scoped to its container - it will pick up rows from another section`)
    }
  }
}

// ---------------------------------------------------------------- 5. syntax
// ------------------------------------------------------------------ per page bridge
// A settings control is only real if the preload that page is given exposes the channel
// it calls. ui/settings.html and ui/newtab.html get page-preload.js; the chrome gets
// preload.js. Missing bridge = the switch saves and nothing happens, which is the worst
// kind of bug this app can have, so it is a defect.
const PAGE_PRELOAD_FOR = { 'ui/settings.html': 'page-preload.js', 'ui/newtab.html': 'page-preload.js', 'ui/renderer.js': 'preload.js' }
for (const [file, which] of Object.entries(PAGE_PRELOAD_FOR)) {
  const src = read(file)
  const bridge = read(which)
  const used = new Set()
  for (const m of src.matchAll(/(?:window\.)?pv\.([a-zA-Z]+)\.([a-zA-Z]+)\s*\(/g)) used.add(m[1] + '.' + m[2])
  for (const use of [...used].sort()) {
    const [ns, fn] = use.split('.')
    const nsBlock = new RegExp('\\b' + ns + ':\\s*\\{').test(bridge)
    const fnHere = new RegExp('\\b' + fn + ':\\s*\\(|\\b' + fn + ':').test(bridge)
    if (!nsBlock || !fnHere) defects.push(`${file} calls pv.${ns}.${fn}() but ${which} does not bridge it - that control is inert`)
  }
}

for (const f of ['main.js', 'download-save.js', 'preload.js', 'page-preload.js', 'spell-page.js', 'ui/renderer.js']) {
  try { execFileSync(process.execPath, ['--check', A(f)], { stdio: 'pipe' }) }
  catch (e) { defects.push(`${f}: does not parse - ${String(e.stderr).split('\n')[0]}`) }
}
try { execFileSync(process.execPath, ['--check', path.join(ROOT, 'tools', 'patch-exe.mjs')], { stdio: 'pipe' }) }
catch (e) { defects.push(`tools/patch-exe.mjs: does not parse - ${String(e.stderr).split('\n')[0]}`) }
try { execFileSync('bash', ['-n', path.join(ROOT, 'tools', 'build-installer.sh')], { stdio: 'pipe' }) }
catch (e) { defects.push(`tools/build-installer.sh: does not parse - ${String(e.stderr).split('\n')[0]}`) }
for (const [label, src] of [['settings.html', settingsHtml], ['newtab.html', newtabHtml]]) {
  const m = src.match(/<script>([\s\S]*)<\/script>/)
  if (!m) { defects.push(`${label}: no script block found`); continue }
  const body = m[1]
  try { execFileSync(process.execPath, ['--check'], { input: body, encoding: 'utf8' }) }
  catch (e) { defects.push(`${label}: inline script does not parse - ${String(e.stderr || e.message).split('\n')[0]}`) }
  const bal = (a, b) => (body.split(a).length - 1) - (body.split(b).length - 1)
  if (bal('{', '}') !== 0) defects.push(`${label}: script braces unbalanced by ${bal('{', '}')}`)
  if (bal('(', ')') !== 0) defects.push(`${label}: script parens unbalanced by ${bal('(', ')')}`)
  for (const fn of uniq([...body.matchAll(/function ([a-zA-Z0-9_]+)\s*\(/g)].map((x) => x[1]))) {
    const decl = body.match(new RegExp(`function ${fn}\\s*\\(`, 'g')) || []
    if (decl.length > 1) defects.push(`${label}: function ${fn} declared ${decl.length} times`)
    const uses = (src.match(new RegExp(`\\b${fn}\\(`, 'g')) || []).length
    if (uses <= decl.length) notes.push(`${label}: function ${fn} is never called`)
  }
}

// ---------------------------------------------------------------- 6. swallowed errors
const catches = [...main.matchAll(/\}\s*catch\s*(?:\([^)]*\))?\s*\{\s*\}/g)]
if (catches.length) notes.push(`main.js has ${catches.length} truly empty catch blocks - document or handle the intentionally ignored failure`)

// ---------------------------------------------------------------- report
console.log(`channels: ${handlers.length} handled, ${bridgeCalls.length} called from bridges, ${sendEvents.length} push events`)
console.log(`settings: ${declared.length} keys, ${uiIds.length} ids on the settings page`)
for (const n of notes) console.log('note  ' + n)
if (defects.length) {
  for (const d of defects) console.log('DEFECT ' + d)
  console.log(`\n${defects.length} defect(s)`)
  process.exit(1)
}
console.log('\nno defects')
