// PsychoVault - main process
// Owns the windows, the tab views, the sessions, the blocker and the stores.
const { app, BrowserWindow, WebContentsView, ipcMain, session, shell, Menu, clipboard, safeStorage, dialog, globalShortcut, powerMonitor, screen, Notification } = require('electron')
const { createSaveDialogQueue } = require('./download-save')
const { buildTranslationUrl } = require('./translation')
const { checkOnStartup: checkForUpdatesOnStartup } = require('./auto-updater')
const path = require('node:path')
const fs = require('node:fs')
const crypto = require('node:crypto')
const net = require('node:net')          // sockets: used to probe the local SOCKS port
const { net: electronNet } = require('electron')   // this is the one that makes requests
const { spawn, execFile } = require('node:child_process')

app.setName('PsychoVault')
if (process.platform === 'win32') app.setAppUserModelId('com.psychocore.psychovault')
const requestDownloadSavePath = createSaveDialogQueue(dialog)

// chrome geometry - must match style.css
const CHROME_H = 84
const RAIL_W = 44
const STATUS_H = 22
const PANEL_W = 330

const USER = app.getPath('userData')
fs.mkdirSync(USER, { recursive: true })

// An uncaught throw in the main process used to pop Electron's modal "A JavaScript
// error occurred in the main process" box - on a browser that is a wall between you and
// your tabs. It now lands in errors.log (visible in Settings - Your data) and the app
// keeps running; the first one in a session also shows a quiet notification.
let mainErrorNotified = false
function logMainError(e) {
  const line = new Date().toISOString() + '  ' + ((e && (e.stack || e.message)) || String(e)) + '\n'
  try { fs.appendFileSync(path.join(USER, 'errors.log'), line) } catch { /* nowhere to write */ }
  console.error('[psychovault]', line.trim())
  if (!mainErrorNotified && Notification.isSupported()) {
    mainErrorNotified = true
    try { new Notification({ title: 'PsychoVault recovered from an error', body: 'Details are in errors.log in your profile folder (Settings - Your data).' }).show() } catch { /* no notifier */ }
  }
}
process.on('uncaughtException', (e) => { try { logMainError(e) } catch { /* dying */ } })
process.on('unhandledRejection', (e) => { try { logMainError(e) } catch { /* dying */ } })

function readJSON(file, fallback) {
  try { return JSON.parse(fs.readFileSync(path.join(USER, file), 'utf8')) } catch { return fallback }
}
function writeJSON(file, value) {
  try { fs.writeFileSync(path.join(USER, file), JSON.stringify(value, null, 1)) } catch (e) {
    console.error('write failed', file, e.message)
  }
}

let bookmarks = readJSON('bookmarks.json', [])
let history = readJSON('history.json', [])
const DEFAULT_SETTINGS = {
  search: 'duckduckgo',
  blockAds: true,
  homepage: 'psychovault://newtab',
  restore: true,
  remember: true,               // off = history is never recorded
  // --- extras, every one of them user-controllable ---
  accent: 'cyan',                 // "mods": accent colour of the whole UI
  hideSidebar: false,             // collapse the GX rail
  highContrast: false,            // higher contrast for browser chrome and Settings
  reduceMotion: false,             // reduce interface and page animations
  showTabCloseButtons: false,     // keep tab close controls visible without hover
  translateTarget: 'en',          // page-translation destination language
  blockAutoplay: false,           // video autoplay needs a click first
  blockThirdPartyCookies: false,  // cross-site Cookie header stripped
  sendDNT: true,                  // DNT + Global Privacy Control headers
  cpuLimit: 'off',                // governor: off | balanced | low
  sleepTabsMin: 0,                // 0 = never; minutes idle before a tab is dropped
  clearOnExit: 'nothing',         // nothing | cache | all
  batterySaver: false,            // GX power save: low priority + fast tab sleep
  vaultEnabled: true,             // local password vault
  vaultLockMin: 5,                // 0 = keep in memory until the app closes
  // --- RGX mode (GX's "enhance videos / enhance images") ---
  enhanceVideo: false,
  enhanceImages: false,
  // --- GX Control third limiter: bandwidth, in KB/s (0 = no cap) ---
  netLimit: 0,
  settingsVersion: 3,           // internal settings schema version
  downloadDir: '',              // initial location for each Save As dialog; empty = system Downloads
  downloadNotify: true,         // system notification when the window is behind something
  // --- GX Panic Button ---
  panicEnabled: true,
  panicUrl: 'https://en.wikipedia.org/wiki/Main_Page',
  // --- sidebar services (GX's messengers/music row) ---
  services: [
    { name: 'Discord', url: 'https://discord.com/app' },
    { name: 'Twitch', url: 'https://www.twitch.tv/directory' },
    { name: 'YouTube Music', url: 'https://music.youtube.com' },
    { name: 'YouTube', url: 'https://www.youtube.com' },
    { name: 'X', url: 'https://x.com/home' },
    { name: 'Reddit', url: 'https://www.reddit.com' },
    { name: 'GitHub', url: 'https://github.com' },
    { name: 'WhatsApp', url: 'https://web.whatsapp.com' },
    { name: 'Telegram', url: 'https://web.telegram.org/a/' },
    { name: 'Spotify', url: 'https://open.spotify.com' }
  ],
  railIcons: true,              // favicons in the rail, fetched from the site itself
  // --- spelling ---
  spellCheck: true,
  spellBuiltin: true,
  spellLangs: '',               // empty = use your system locale; csv like "en-gb,de-de"
  spellDictUrl: '',             // mirror or file:// folder holding <lang>.bdic files
  spellUserWords: [],           // what you added from a right-click menu
  // --- extensions (unpacked folders and .crx files, loaded per session) ---
  extensions: [],               // [{ dir, enabled, id }] - what is installed, in install order
  mediaKeys: true,
  mediaAutoPause: true,
  // --- keyboard sounds, the GX personality bit ---
  uiSounds: false,
  soundVol: 0.4,
  // --- hardening (privacy, all off-by-default where it could break a site) ---
  webRtc: 'publiconly',           // default | publiconly | disableudp
  doh: 'off',                     // off | automatic | secure
  dohTemplate: '',
  stripReferrer: true,            // drop cross-site Referer
  denyPerms: 'geolocation, media, clipboard-read, midi, sync-xhr, wake-lock, notifications',
  containers: [
    { name: 'Work', color: '#f472b6' },
    { name: 'Shopping', color: '#38bdf8' },
    { name: 'Social', color: '#a78bfa' },
    { name: 'Banking', color: '#22c55e' }
  ],
  // --- mods-lite ---
  modCss: '',
  batteryAuto: false,
  speedDial: [
    { title: 'GitHub', url: 'https://github.com' },
    { title: 'YouTube', url: 'https://youtube.com' },
    { title: 'Reddit', url: 'https://reddit.com' },
    { title: 'Wikipedia', url: 'https://wikipedia.org' },
    { title: 'Mail', url: 'https://mail.google.com' },
    { title: 'News', url: 'https://news.ycombinator.com' }
  ]
}
let settings = Object.assign({}, DEFAULT_SETTINGS, readJSON('settings.json', {}))
// Save As is now mandatory for every download, not a preference that can be left off
// in an old profile. Remove the retired setting so legacy `false` values cannot silently
// bypass the location picker.
;(function migrateSettings() {
  const savedVersion = Number(settings.settingsVersion) || 1
  const hadLegacySavePreference = Object.prototype.hasOwnProperty.call(settings, 'askSaveLocation')
  if (savedVersion >= 3 && !hadLegacySavePreference) return
  delete settings['askSaveLocation']
  settings.settingsVersion = 3
  writeJSON('settings.json', settings)
})()

// Chromium reads these once, at startup, so a change needs a relaunch - the
// settings page says so and offers the button.
function applyHardeningSwitches() {
  const cl = app.commandLine
  const policy = { default: '', publiconly: 'default_public_interface_only', disableudp: 'disable_non_proximity_udp' }
  const want = policy[settings.webRtc] || ''
  if (want) cl.appendSwitch('force-webrtc-ip-handling-policy', want)
  if (settings.doh === 'automatic') cl.appendSwitch('dns-over-https-mode', 'automatic')
  else if (settings.doh === 'secure' && settings.dohTemplate) {
    cl.appendSwitch('dns-over-https-mode', 'secure')
    cl.appendSwitch('dns-over-https-template', String(settings.dohTemplate).trim())
  }
}
applyHardeningSwitches()

// ---------------------------------------------------------------- spellchecker
// Two things make spellcheck "not work" in an Electron app: nobody enables the language
// list (Chromium ships no dictionaries, they are downloaded on demand), and an app that
// builds its own edit context menu hides the suggestions Chromium would have offered.
const spell = { enabled: true, languages: [], count: 0, missing: '', dictUrl: '' }

// Everything an Electron build can do about dictionaries is flip a flag: there is no
// downloader, <profile>/Dictionaries is not read and the download URL is never fetched
// (checked against this Electron: no downloadDictionary, no isDictionaryAvailable, zero
// requests to the URL we set). So PsychoVault carries its own word list and runs the check
// in the page - that is what puts the red line under a typo and what feeds the edit menu.
const SPELL_PAGE = (() => {
  try { return fs.readFileSync(path.join(__dirname, 'spell-page.js'), 'utf8') } catch (e) { logMainError(e); return '' }
})()
const SPELL_WORDS = (() => {
  try { return fs.readFileSync(path.join(__dirname, 'spell-words.txt'), 'utf8').split('\n').map((x) => x.trim()).filter(Boolean) }
  catch { return [] }
})()
function spellBuiltinOn() {
  return settings.spellCheck !== false && settings.spellBuiltin !== false && SPELL_WORDS.length > 0 && !!SPELL_PAGE
}
function applySpellToPage(wc) {
  if (!wc || wc.isDestroyed()) return
  const seeded = !!wc._pvSpellSeeded
  const cfg = { on: spellBuiltinOn(), user: settings.spellUserWords || [] }
  if (!seeded) { cfg.words = SPELL_WORDS; cfg.reseed = true }
  const code = SPELL_PAGE + '\n;window.__pvSpellBoot(' + JSON.stringify(cfg) + ')'
  wc.executeJavaScript(code).then(() => { wc._pvSpellSeeded = true }).catch(() => { /* page refused us */ })
}
function applySpellToAllPages() {
  for (const w of mainWindows) for (const t of w.tabs.values()) applySpellToPage(t.view.webContents)
}
function tellPages(fn, arg) {
  const js = 'window.' + fn + ' && window.' + fn + '(' + JSON.stringify(arg) + ')'
  for (const w of mainWindows) {
    for (const t of w.tabs.values()) t.view.webContents.executeJavaScript(js).catch(() => { /* page or field gone */ })
  }
}

// Ask the page what it thinks of the word under the caret. Only used when Chromium had
// nothing to say, which - without dictionaries - is the normal case here.
function builtinSpellAt(wc) {
  if (!wc || wc.isDestroyed() || !spellBuiltinOn()) return Promise.resolve(null)
  return wc.executeJavaScript('window.__pvSpellGet ? window.__pvSpellGet() : null').catch(() => null)
}

function trackSession(ses) {
  if (!ses) return
  if (!liveSessions.has(ses)) liveSessions.add(ses)
  if (typeof ses.on === 'function' && !ses._pvSpellWatched) {
    ses._pvSpellWatched = true
    ses.on('spellcheck-language-not-available', (_e, langs) => {
      spell.missing = (Array.isArray(langs) ? langs : [langs]).join(', ')
      broadcast('spell', { ...spell })
    })
  }
}

function spellWant() {
  const raw = String(settings.spellLangs || '').split(',').map((x) => x.trim()).filter(Boolean)
  if (raw.length) return raw
  const loc = String(app.getLocale() || 'en-US')
  return [loc, loc.split('-')[0]].filter((v, i, a) => v && a.indexOf(v) === i)
}

// "de-de" has to become 'de', "en-us" has to become 'en-US' - and a tag Chromium has no
// dictionary for must never reach setSpellCheckerLanguages, which throws on those.
function resolveLangs(want, avail) {
  const list = Array.isArray(avail) ? avail : []
  const byLower = new Map(list.map((a) => [String(a).toLowerCase(), a]))
  const out = []
  const miss = []
  for (const raw of want) {
    const v = String(raw).trim().toLowerCase()
    if (!v) continue
    let hit = byLower.get(v)
    if (!hit) {
      const base = v.split('-')[0]
      hit = byLower.get(base)
      if (!hit && base === 'en') hit = byLower.get('en-GB') || byLower.get('en-US') || byLower.get('en')
    }
    if (hit) { if (!out.includes(hit)) out.push(hit) } else miss.push(String(raw))
  }
  return { langs: out, miss }
}

function applySpellPolicy(ses) {
  if (!ses || typeof ses.setSpellCheckerEnabled !== 'function') return
  try {
    const on = settings.spellCheck !== false
    ses.setSpellCheckerEnabled(on)
    spell.enabled = on
    const avail = ses.availableSpellCheckerLanguages || []
    spell.count = avail.length
    spell.dictUrl = String(settings.spellDictUrl || '').trim()
    if (!on) { spell.languages = []; return }
    const want = spellWant()
    for (const ses2 of [ses]) { /* one pass, keeps the shape readable */ }
    const res = resolveLangs(want, avail)
    if (res.langs.length) ses.setSpellCheckerLanguages(res.langs)
    spell.languages = res.langs
    spell.missing = res.miss.length ? res.miss.join(', ') : (res.langs.length ? '' : want.join(', '))
    spell.count = avail.length
    const url = spell.dictUrl
    if (url) ses.setSpellCheckerDictionaryDownloadURL(/\/$/.test(url) ? url : url + '/')
  } catch (e) { logMainError(e) }
}

function applySpellPolicyAll() {
  for (const ses of liveSessions) applySpellPolicy(ses)
  broadcast('spell', { ...spell })
  for (const w of mainWindows) w.sendState()
}

function addSpellWord(ses, word) {
  const w = String(word || '').trim().slice(0, 64)
  if (!w || !/[a-z]/i.test(w)) return { ok: false, why: 'nothing to add' }
  let added = false
  try { added = ses.addWordToSpellCheckerDictionary(w) } catch (e) { logMainError(e) }
  const list = new Set(Array.isArray(settings.spellUserWords) ? settings.spellUserWords : [])
  list.add(w.toLowerCase())
  settings = { ...settings, spellUserWords: [...list].sort().slice(0, 2000) }
  writeJSON('settings.json', settings)
  return { ok: added, word: w, note: added ? '' : 'the spellchecker refused it - usually no dictionary is loaded yet' }
}

function dropSpellWord(ses, word) {
  const w = String(word || '').trim().toLowerCase()
  let removed = false
  try { removed = ses.removeWordFromSpellCheckerDictionary(w) } catch (e) { logMainError(e) }
  const list = (Array.isArray(settings.spellUserWords) ? settings.spellUserWords : []).filter((x) => x !== w)
  settings = { ...settings, spellUserWords: list }
  writeJSON('settings.json', settings)
  return { ok: removed, word: w }
}

// ---------------------------------------------------------------- rail services
// Brand list for the rail. Favicons come from the site itself, never from a third-party
// icon service - a browser that uploads nothing should not phone a graph when it paints.
const SERVICE_CATALOGUE = [
  { name: 'Discord', url: 'https://discord.com/app', color: '#5865f2' },
  { name: 'Twitch', url: 'https://www.twitch.tv/directory', color: '#9146ff' },
  { name: 'YouTube Music', url: 'https://music.youtube.com', color: '#ff0033' },
  { name: 'YouTube', url: 'https://www.youtube.com', color: '#ff0033' },
  { name: 'X', url: 'https://x.com/home', color: '#2f3336' },
  { name: 'Reddit', url: 'https://www.reddit.com', color: '#ff4500' },
  { name: 'GitHub', url: 'https://github.com', color: '#6e7681' },
  { name: 'WhatsApp', url: 'https://web.whatsapp.com', color: '#25d366' },
  { name: 'Telegram', url: 'https://web.telegram.org/a/', color: '#229ed9' },
  { name: 'Spotify', url: 'https://open.spotify.com', color: '#1db954' },
  { name: 'Slack', url: 'https://app.slack.com/client', color: '#4a154b' },
  { name: 'Messenger', url: 'https://www.messenger.com', color: '#0084ff' },
  { name: 'Bluesky', url: 'https://bsky.app', color: '#118ee7' },
  { name: 'Instagram', url: 'https://www.instagram.com', color: '#c13584' },
  { name: 'Hacker News', url: 'https://news.ycombinator.com', color: '#ff6600' },
  { name: 'Gmail', url: 'https://mail.google.com', color: '#ea4335' },
  { name: 'LinkedIn', url: 'https://www.linkedin.com/feed/', color: '#0a66c2' },
  { name: 'Steam', url: 'https://store.steampowered.com', color: '#66c0f4' }
]
const RAIL_MAX = 14

function hostOf(u) { try { return new URL(u).hostname.toLowerCase() } catch { return '' } }
function originOf(u) { try { const o = new URL(u).origin; return /^https?:/i.test(o) ? o : '' } catch { return '' } }
function brandFor(svc) {
  const h = hostOf(svc && svc.url)
  const hit = SERVICE_CATALOGUE.find((c) => {
    const ch = hostOf(c.url)
    return ch && (h === ch || h.endsWith('.' + ch) || ch.endsWith('.' + h))
  })
  return hit ? hit.color : null
}
function servicesPayload() {
  const list = Array.isArray(settings.services) ? settings.services : []
  return list.slice(0, RAIL_MAX).map((sv) => ({
    name: String(sv.name || hostOf(sv.url) || 'Service').slice(0, 24),
    url: String(sv.url || ''),
    host: hostOf(sv.url),
    origin: originOf(sv.url),
    color: sv.color || brandFor(sv) || null,
    initials: String(sv.name || hostOf(sv.url) || '?').replace(/^https?:\/\//, '').slice(0, 2).toUpperCase()
  }))
}

const ICON_DIR = path.join(USER, 'icons')
const iconPending = new Set()
let iconBroadcastTimer = null
function iconFileFor(origin) { return path.join(ICON_DIR, crypto.createHash('sha1').update(String(origin).toLowerCase()).digest('hex').slice(0, 20)) }
function mimeFor(buf) {
  if (!buf || buf.length < 12) return null
  if (buf[0] === 0x89 && buf[1] === 0x50) return 'image/png'
  if (buf.slice(0, 3).toString('latin1') === 'GIF') return 'image/gif'
  if (buf[0] === 0xff && buf[1] === 0xd8) return 'image/jpeg'
  // .ico layout: reserved=0 (u16), type=1 for icons / 2 for cursors (u16), then the count
  if (buf.length >= 6 && buf.readUInt16LE(0) === 0 && (buf.readUInt16LE(2) === 1 || buf.readUInt16LE(2) === 2)) return 'image/x-icon'
  if (/<svg/i.test(buf.slice(0, 200).toString('latin1'))) return 'image/svg+xml'
  return null
}
function httpGrab(url, asText) {
  return new Promise((resolve) => {
    let req
    try { req = electronNet.request({ url, method: 'GET', redirects: 'follow', useSessionCookies: false }) } catch (e) { logMainError(e); return resolve(null) }
    const chunks = []
    let size = 0
    const timer = setTimeout(() => { try { req.abort() } catch { /* request may already be closed */ } resolve(null) }, 7000)
    req.on('response', (res) => {
      if (res.statusCode >= 400) { clearTimeout(timer); return resolve(null) }
      res.on('data', (c) => { size += c.length; if (size > 512 * 1024) { try { res.destroy() } catch { /* response may already be destroyed */ } return resolve(null) }; chunks.push(c) })
      res.on('end', () => { clearTimeout(timer); resolve(asText ? Buffer.concat(chunks).toString('utf8') : Buffer.concat(chunks)) })
      res.on('error', () => { clearTimeout(timer); resolve(null) })
    })
    req.on('error', () => { clearTimeout(timer); resolve(null) })
    req.setHeader('User-Agent', 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36')
    req.end()
  })
}
// favicon.ico first, then whatever <link rel="icon"> the document points at. Fetched from
// the site itself over its own origin - no third-party icon service, nothing leaked.
async function fetchIconFor(origin) {
  if (!origin) return false
  if (iconPending.has(origin)) return false
  iconPending.add(origin)
  try {
    const base = origin
    let buf = await httpGrab(base + '/favicon.ico')
    if (!buf || !mimeFor(buf)) {
      const html = await httpGrab(base + '/', true)
      if (html) {
        const m = html.match(/<link[^>]+rel=["']?(?:shortcut\s+)?icon["']?[^>]*href=["']([^"']+)/i) ||
                  html.match(/<link[^>]+href=["']([^"']+)["'][^>]*rel=["']?(?:shortcut\s+)?icon/i)
        if (m) {
          let href = m[1].replace(/&amp;/g, '&')
          if (href.startsWith('//')) href = 'https:' + href
          else if (href.startsWith('/')) href = base + href
          else if (!/^https?:/i.test(href)) href = base + '/' + href.replace(/^\.\//, '')
          const got = await httpGrab(href)
          if (got && mimeFor(got)) buf = got
        }
      }
    }
    if (!buf || !mimeFor(buf)) return false
    fs.mkdirSync(ICON_DIR, { recursive: true })
    const file = iconFileFor(origin)
    if (!fs.existsSync(file)) fs.writeFileSync(file, buf)
    if (!iconBroadcastTimer) {
      iconBroadcastTimer = setTimeout(() => {
        iconBroadcastTimer = null
        broadcast('icons', { at: Date.now() })
        for (const w of mainWindows) w.sendState()
      }, 150)
    }
    return true
  } catch (e) { logMainError(e); return false } finally {
    iconPending.delete(origin)
  }
}
function railIcons() {
  const out = {}
  for (const svc of servicesPayload()) {
    if (!svc.origin) continue
    try {
      const file = iconFileFor(svc.origin)
      if (!fs.existsSync(file) || settings.railIcons === false) continue
      const buf = fs.readFileSync(file)
      const mime = mimeFor(buf)
      if (mime) out[svc.origin] = `data:${mime};base64,${buf.toString('base64')}`
    } catch { /* half-written file, ignore */ }
  }
  return out
}
function updateServiceIcons() {
  if (settings.railIcons === false) return
  for (const svc of servicesPayload()) {
    if (!svc.origin || fs.existsSync(iconFileFor(svc.origin))) continue
    fetchIconFor(svc.origin).catch((e) => logMainError(e))
  }
}


// ---------------------------------------------------------------- extensions
// Electron loads unpacked extensions per session. Store extensions arrive as .crx, which
// Electron cannot load, so this unpacks the zip payload itself and loads the folder.
// What it cannot do is invent APIs Electron does not implement - chrome.proxy above all,
// which is exactly how a browser VPN extension connects. So every manifest is read on
// install and any dependency the engine cannot satisfy is reported, in the panel, before use.
const EXT_ROOT = path.join(USER, 'extensions')

// Permissions this engine implements (Electron's "Chrome Extension Support" page). Anything
// outside the list is either unknown to it or known to be missing - the two are kept apart
// below rather than lumped into "might not work".
const EXT_KNOWN = new Set([
  'storage', 'tabs', 'webRequest', 'webRequestBlocking', 'scripting', 'runtime', 'management',
  'idle', 'notifications', 'privacy', 'webRequestAuthProvider', 'contextMenus', 'cookies',
  'unlimitedStorage', 'activeTab', 'alarms', 'i18n', 'tts', 'webNavigation', 'devtools'
])
// Named with what the extension would have to be able to do, so the panel reads as a sentence.
const EXT_MISSING = {
  proxy: 'chrome.proxy - it has to set the browser-wide proxy, which is how a browser VPN extension connects. Electron does not implement this API',
  vpnProvider: 'chrome.vpnProvider - a browser-managed VPN tunnel, which Electron does not implement',
  declarativeNetRequest: 'declarativeNetRequest - MV3 network rules, which Electron does not implement (MV2 chrome.webRequest blocking does work)',
  declarativeNetRequestWithHostAccess: 'declarativeNetRequest - MV3 network rules, which Electron does not implement',
  nativeMessaging: 'native messaging - talking to a program installed on your PC, which Electron does not implement',
  debugger: 'chrome.debugger, which Electron does not implement',
  identity: 'chrome.identity - Google sign-in, which Electron does not implement',
  tabCapture: 'tab capture, which Electron does not implement',
  desktopCapture: 'desktop capture, which Electron does not implement',
  offscreen: 'offscreen documents, which Electron does not implement',
  sidePanel: 'the side panel UI, which this browser has no room for'
}

function extSessions() {
  const out = [session.defaultSession]
  const parts = ['persist:psychovault'].concat((settings.containers || []).map((c) => 'persist:pv-c-' + String(c.name).toLowerCase()))
  for (const p of parts) { try { out.push(session.fromPartition(p)) } catch (e) { logMainError(e) } }
  return out
}

function extReadManifest(dir) {
  return JSON.parse(fs.readFileSync(path.join(dir, 'manifest.json'), 'utf8'))
}

// Read the zip payload out of a .crx. Electron refuses .crx by design, so the browser does
// what Chrome's installer does: drop the CRX header and unzip the rest. The signature block
// is NOT verified - the trust is the same as downloading and unzipping that file yourself.
function extCrxPayload(file) {
  const buf = fs.readFileSync(file)
  if (buf.length < 16 || buf.slice(0, 4).toString() !== 'Cr24') throw new Error('not a .crx file')
  const version = buf.readUInt32LE(4)
  let off
  if (version === 3) off = 12 + buf.readUInt32LE(8)
  else if (version === 2) off = 16 + buf.readUInt32LE(8) + buf.readUInt32LE(12)
  else throw new Error('unsupported .crx version ' + version)
  if (off >= buf.length) throw new Error('truncated .crx file')
  return buf.slice(off)
}

// Minimal zip reader: central directory, stored and deflate entries. Enough for an extension;
// a zip64 archive is refused rather than half-read.
function extUnzip(zip, dest) {
  const zlib = require('zlib')
  const EOCD = 0x06054b50
  let eocd = -1
  for (let i = zip.length - 22; i >= Math.max(0, zip.length - 70000); i--) {
    if (zip.readUInt32LE(i) === EOCD) { eocd = i; break }
  }
  if (eocd < 0) throw new Error('zip: end of central directory not found')
  const count = zip.readUInt16LE(eocd + 10)
  let p = zip.readUInt32LE(eocd + 16)
  if (count === 0xffff || p === 0xffffffff) throw new Error('zip64 archives are not supported')
  const root = path.resolve(dest)
  let files = 0
  for (let n = 0; n < count; n++) {
    if (zip.readUInt32LE(p) !== 0x02014b50) throw new Error('zip: bad central directory record')
    const method = zip.readUInt16LE(p + 10)
    const csize = zip.readUInt32LE(p + 20)
    const usize = zip.readUInt32LE(p + 24)
    const nameLen = zip.readUInt16LE(p + 28)
    const extraLen = zip.readUInt16LE(p + 30)
    const commentLen = zip.readUInt16LE(p + 32)
    const local = zip.readUInt32LE(p + 42)
    const name = zip.slice(p + 46, p + 46 + nameLen).toString('utf8').replace(/\\/g, '/')
    p += 46 + nameLen + extraLen + commentLen
    const target = path.resolve(root, name)
    if (target !== root && !target.startsWith(root + path.sep)) throw new Error('zip: entry escapes the destination: ' + name)
    if (/\/$/.test(name)) { fs.mkdirSync(target, { recursive: true }); continue }
    // local header is 30 bytes: sig(4) ver(2) flags(2) method(2) time(2) date(2) crc(4) csize(4) usize(4) nameLen(2) extraLen(2)
    const lh = local + 30 + zip.readUInt16LE(local + 26) + zip.readUInt16LE(local + 28)
    const data = zip.slice(lh, lh + csize)
    let out
    if (method === 0) out = data
    else if (method === 8) out = zlib.inflateRawSync(data)
    else throw new Error('zip: unsupported compression method ' + method + ' for ' + name)
    if (usize && out.length !== usize) throw new Error('zip: size mismatch for ' + name)
    fs.mkdirSync(path.dirname(target), { recursive: true })
    fs.writeFileSync(target, out)
    files++
  }
  return files
}

function extSlug(name, tag) {
  const base = String(name || 'extension').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 40) || 'extension'
  const t = String(tag || '').replace(/[^a-z0-9]/gi, '').slice(-6).toLowerCase()
  return t ? base + '-' + t : base
}

// What this extension needs, and whether this engine can give it to them.
function extAnalyse(dir) {
  const manifest = extReadManifest(dir)
  const perms = []
  for (const k of ['permissions', 'optional_permissions']) {
    const v = manifest[k]
    if (Array.isArray(v)) for (const p of v) if (typeof p === 'string') perms.push(p)
  }
  const blockers = []
  for (const p of perms) if (EXT_MISSING[p]) blockers.push(EXT_MISSING[p])
  const notes = []
  const action = manifest.action || manifest.browser_action
  if (action) notes.push('it has a toolbar button, and this browser has no toolbar - use "Open its window" to reach the popup')
  if (manifest.options_ui || manifest.options_page) notes.push('it has an options page, reachable from here')
  if (manifest.background && manifest.background.service_worker) notes.push('manifest v3 background service worker (works, but fewer lifecycle events than Chrome)')
  if (manifest.background && manifest.background.scripts) notes.push('manifest v2 background page')
  const unlisted = perms.filter((p) => !EXT_KNOWN.has(p) && !EXT_MISSING[p] && !/:\/\//.test(p) && p !== '<all_urls>')
  if (unlisted.length) notes.push('it asks for ' + unlisted.join(', ') + ' - outside Electron\'s supported list, may do nothing')
  return {
    manifest, name: manifest.name || path.basename(dir), version: manifest.version || '',
    mv: manifest.manifest_version || 2, blockers, notes,
    popup: (action && action.default_popup) || '',
    options: manifest.options_page || (manifest.options_ui && manifest.options_ui.page) || ''
  }
}

function extRecords() {
  return (Array.isArray(settings.extensions) ? settings.extensions : []).filter((e) => e && e.dir)
}

async function extLoadOne(dir) {
  const results = []
  for (const ses of extSessions()) {
    try {
      const info = await ses.loadExtension(dir, { allowFileAccess: true })
      results.push({ ok: true, id: info.id })
    } catch (e) {
      results.push({ ok: false, error: String((e && e.message) || e) })
      logMainError(e)
    }
  }
  const bad = results.filter((r) => !r.ok)
  return { ok: bad.length === 0, id: (results.find((r) => r.ok) || {}).id || '', error: bad.length ? bad[0].error : '' }
}

function extUnloadOne(dir) {
  let real = dir
  try { if (fs.existsSync(dir)) real = fs.realpathSync(dir) } catch (e) { /* keep as-is */ }
  for (const ses of extSessions()) {
    let list = []
    try { list = ses.getAllExtensions() } catch (e) { continue }
    for (const e of list) {
      let p = e.path || ''
      try { if (p && fs.existsSync(p)) p = fs.realpathSync(p) } catch (err) { /* keep */ }
      if (p === real || e.path === dir) { try { ses.removeExtension(e.id) } catch (err) { logMainError(err) } }
    }
  }
}

async function extSync() {
  let loaded = 0
  for (const rec of extRecords()) {
    if (!rec.enabled) { extUnloadOne(rec.dir); continue }
    if (!fs.existsSync(path.join(rec.dir, 'manifest.json'))) continue
    const r = await extLoadOne(rec.dir)
    if (r.ok) { loaded++; rec.id = r.id; delete rec.error }
    else rec.error = r.error
  }
  return loaded
}

async function extList() {
  const out = []
  for (const rec of extRecords()) {
    let info
    try { info = extAnalyse(rec.dir) } catch (e) {
      info = { name: path.basename(rec.dir), version: '', mv: 0, blockers: ['its manifest.json cannot be read: ' + e.message], notes: [], popup: '', options: '' }
    }
    out.push({
      dir: rec.dir, enabled: !!rec.enabled, id: rec.id || '',
      name: info.name, version: info.version, mv: info.mv,
      blockers: info.blockers, notes: info.notes, popup: info.popup, options: info.options,
      error: rec.error || '', loaded: !!rec.enabled && !rec.error
    })
  }
  return out
}

function extSave(list) {
  settings = { ...settings, extensions: list }
  writeJSON('settings.json', settings)
  for (const w of mainWindows) w.sendState()
}

async function extPublish() {
  broadcast('extensions', { items: await extList() })
}

async function extAddDir(dir) {
  const info = extAnalyse(dir)
  fs.mkdirSync(EXT_ROOT, { recursive: true })
  const dest = path.join(EXT_ROOT, extSlug(info.name, Date.now().toString(36)))
  fs.cpSync(dir, dest, { recursive: true })
  const rec = { dir: dest, enabled: true }
  const load = await extLoadOne(dest)
  if (load.ok) rec.id = load.id; else rec.error = load.error
  extSave(extRecords().concat([rec]))
  await extPublish()
  return { ok: true, name: info.name, blockers: info.blockers, error: load.error || '' }
}

async function extAddCrx(file) {
  fs.mkdirSync(EXT_ROOT, { recursive: true })
  const tmp = path.join(EXT_ROOT, '.incoming-' + Date.now().toString(36))
  fs.mkdirSync(tmp, { recursive: true })
  try {
    const files = extUnzip(extCrxPayload(file), tmp)
    const info = extAnalyse(tmp)
    const dest = path.join(EXT_ROOT, extSlug(info.name, Date.now().toString(36)))
    fs.renameSync(tmp, dest)
    const rec = { dir: dest, enabled: true }
    const load = await extLoadOne(dest)
    if (load.ok) rec.id = load.id; else rec.error = load.error
    extSave(extRecords().concat([rec]))
    await extPublish()
    return { ok: true, name: info.name, files, blockers: info.blockers, error: load.error || '' }
  } finally {
    try { if (fs.existsSync(tmp)) fs.rmSync(tmp, { recursive: true, force: true }) } catch (e) { /* gone */ }
  }
}

ipcMain.handle('ext:list', () => extList())
ipcMain.handle('ext:addfolder', async (e) => {
  const w = findWin(e)
  const picked = dialog.showOpenDialogSync(w && w.win && !w.win.isDestroyed() ? w.win : null, {
    title: 'Load an unpacked extension', properties: ['openDirectory'], buttonLabel: 'Load'
  })
  if (!picked || !picked[0]) return { ok: false, error: 'cancelled' }
  try { return await extAddDir(picked[0]) } catch (err) { return { ok: false, error: err.message } }
})
ipcMain.handle('ext:addcrx', async (e) => {
  const w = findWin(e)
  const picked = dialog.showOpenDialogSync(w && w.win && !w.win.isDestroyed() ? w.win : null, {
    title: 'Install an extension from a .crx file', properties: ['openFile'],
    filters: [{ name: 'Chrome extension', extensions: ['crx'] }, { name: 'All files', extensions: ['*'] }]
  })
  if (!picked || !picked[0]) return { ok: false, error: 'cancelled' }
  try { return await extAddCrx(picked[0]) } catch (err) { return { ok: false, error: err.message } }
})
ipcMain.handle('ext:remove', async (e, dir) => {
  const rec = extRecords().find((x) => x.dir === dir)
  if (!rec) return { ok: false, error: 'no such extension' }
  extUnloadOne(rec.dir)
  try { if (rec.dir.indexOf(EXT_ROOT) === 0) fs.rmSync(rec.dir, { recursive: true, force: true }) } catch (err) { logMainError(err) }
  extSave(extRecords().filter((x) => x.dir !== dir))
  await extPublish()
  return { ok: true, items: await extList() }
})
ipcMain.handle('ext:toggle', async (e, o) => {
  const rec = extRecords().find((x) => x.dir === (o && o.dir))
  if (!rec) return { ok: false, error: 'no such extension' }
  rec.enabled = !!(o && o.enabled)
  delete rec.error
  if (rec.enabled) {
    const r = await extLoadOne(rec.dir)
    if (r.ok) rec.id = r.id; else rec.error = r.error
  } else extUnloadOne(rec.dir)
  extSave(extRecords())
  await extPublish()
  return { ok: true, items: await extList() }
})
ipcMain.handle('ext:openpage', (e, o) => {
  const w = findWin(e)
  const rec = extRecords().find((x) => x.dir === (o && o.dir))
  if (!w || !rec || !rec.id) return { ok: false }
  const page = String((o && o.page) || '').replace(/^\/+/, '')
  if (!page) return { ok: false }
  const href = 'chrome-extension://' + rec.id + '/' + page
  w.createTab(href)
  return { ok: true, url: href }
})

// ---------------------------------------------------------------- blocklist
// Domain list shipped with the app; loaded once, matched by hostname.
let blockSet = new Set()
function loadBlocklist() {
  if (!settings.blockAds) return
  const file = path.join(__dirname, 'blocklist.json')
  try {
    const list = JSON.parse(fs.readFileSync(file, 'utf8'))
    blockSet = new Set(list)
    console.log(`blocklist: ${blockSet.size} domains`)
  } catch (e) {
    console.log('blocklist not loaded:', e.message)
  }
}

function safe(fn, fallback) {
  try { const v = fn(); return v === undefined ? fallback : v } catch { return fallback }
}

function hostOf(url) {
  try { return new URL(url).hostname.replace(/^www\./, '') } catch { return '' }
}

function isBlocked(url) {
  if (!settings.blockAds || blockSet.size === 0) return false
  const h = hostOf(url)
  if (!h) return false
  if (blockSet.has(h)) return true
  const parts = h.split('.')
  for (let i = 1; i < parts.length - 1; i++) {
    if (blockSet.has(parts.slice(i).join('.'))) return true
  }
  return false
}

// ---------------------------------------------------------------- GX-style extras
const liveSessions = new Set()   // every session PsychoVault has touched, for policy re-apply

function etld1(host) {
  const parts = String(host || '').split('.')
  return parts.length <= 2 ? String(host || '') : parts.slice(-2).join('.')
}

// True when a subresource comes from a different registrable domain than its caller.
function crossSite(initiator, target) {
  try {
    if (!initiator) return false
    return etld1(new URL(initiator).hostname) !== etld1(new URL(target).hostname)
  } catch { return false }
}

// CPU limiter: lowers the priority of the whole process tree, the way GX's does.
function applyCpuLimit() {
  try {
    if (settings.batterySaver) {
      // power save: low priority and tabs sleep fast, nothing else changes
      app.setPriority(settings.cpuLimit === 'low' ? 'idle' : 'below-normal')
      return
    }
    if (settings.cpuLimit === 'low') app.setPriority('idle')
    else if (settings.cpuLimit === 'balanced') app.setPriority('below-normal')
    else app.setPriority('normal')
  } catch (e) { /* the OS may refuse; not fatal */ }
}

function sleepLimitMinutes() {
  const base = Number(settings.sleepTabsMin) || 0
  if (!settings.batterySaver) return base
  return base === 0 ? 3 : Math.min(base, 3)
}

// Autoplay + header policy, re-applied whenever settings change.
function applySessionPolicy(ses) {
  if (!liveSessions.has(ses)) liveSessions.add(ses)
  try {
    ses.setAutoplayPolicy(settings.blockAutoplay
      ? 'document-user-activation-required'
      : 'no-user-gesture-required')
  } catch (e) { /* policy unavailable on this build */ }
  if (ses._pvHdr) return
  ses._pvHdr = true
  ses.webRequest.onBeforeSendHeaders((details, cb) => {
    const h = details.requestHeaders
    let touched = false
    if (settings.sendDNT) { h['DNT'] = '1'; h['Sec-GPC'] = '1'; touched = true }
    if (settings.blockThirdPartyCookies && details.resourceType !== 'mainFrame' &&
        crossSite(details.initiator, details.url)) {
      for (const k of Object.keys(h)) if (k.toLowerCase() === 'cookie') { delete h[k]; touched = true }
    }
    if (settings.stripReferrer && details.resourceType !== 'mainFrame' &&
        crossOrigin(details.initiator, details.url)) {
      for (const k of Object.keys(h)) if (k.toLowerCase() === 'referer') { delete h[k]; touched = true }
    }
    cb(touched ? { requestHeaders: h } : {})
  })
}

// The deny list is a setting, not a hard-coded choice.
// Cookie policy is registrable-domain based, because that is what cookies are keyed
// by. Referrer policy is origin based, because a different port is a different site.
function crossOrigin(initiator, target) {
  try {
    if (!initiator || initiator === 'null') return true
    return new URL(initiator).origin !== new URL(target).origin
  } catch { return false }
}

// clipboard.readText() is documented to return a string, and on a real desktop it can
// hand back something else when the clipboard owner is mid-write or holds a format
// Electron cannot map. One menu item must not be able to kill the browser over that.
// Electron 45 returns a Promise from clipboard.readText() on this host, so the previous
// synchronous version compared a Promise against 'string' and always answered "empty" -
// which quietly disabled Paste and go everywhere. Await it, and still tolerate a plain
// string for builds where the call is synchronous.
async function clipText() {
  try {
    const v = await clipboard.readText()
    return typeof v === 'string' ? v.trim() : ''
  } catch (e) { logMainError(e); return '' }
}

function permDenied(name) {
  const list = String(settings.denyPerms || '').split(',').map(x => x.trim().toLowerCase()).filter(Boolean)
  return list.includes(String(name).toLowerCase())
}

// ---------------------------------------------------------------- password vault
// Two encryption paths, both real: the OS keychain via safeStorage, or a master
// password through scrypt + AES-256-GCM. If neither is available the vault refuses
// to write, because a plaintext password file is worse than no vault at all.
const VAULT_FILE = 'vault.bin'
const VAULT_KEY = 'vault.key.json'
let vaultCache = null
let vaultKey = null          // derived key, only while unlocked
let vaultLocked = true
let vaultTimer = null
let clipboardLockTimer = null

// Electron 45 deprecates the synchronous probe; cache the async answer at
// startup and fall back to the sync one if a build lacks it.
let ssAvailable = false
async function refreshSafeStorage() {
  try {
    if (typeof safeStorage.isAsyncEncryptionAvailable === 'function') {
      ssAvailable = await safeStorage.isAsyncEncryptionAvailable()
    } else {
      ssAvailable = safeStorage.isEncryptionAvailable()
    }
  } catch {
    try { ssAvailable = safeStorage.isEncryptionAvailable() } catch { ssAvailable = false }
  }
  return ssAvailable
}
function vaultSafeOk() {
  return ssAvailable
}
function vaultMethod() {
  if (fs.existsSync(path.join(USER, VAULT_KEY))) return 'master'
  return vaultSafeOk() ? 'keychain' : 'none'
}
function vaultDerive(pw, saltB64) {
  return crypto.scryptSync(String(pw), Buffer.from(saltB64, 'base64'), 32, { N: 16384, r: 8, p: 1, maxmem: 64 * 1024 * 1024 })
}
function vaultSeal(json, key) {
  const iv = crypto.randomBytes(12)
  const c = crypto.createCipheriv('aes-256-gcm', key, iv)
  const body = Buffer.concat([c.update(json, 'utf8'), c.final(), c.getAuthTag()])
  return Buffer.concat([iv, body])
}
function vaultOpen(buf, key) {
  const iv = buf.subarray(0, 12)
  const tag = buf.subarray(buf.length - 16)
  const body = buf.subarray(12, buf.length - 16)
  const d = crypto.createDecipheriv('aes-256-gcm', key, iv)
  d.setAuthTag(tag)
  return Buffer.concat([d.update(body), d.final()]).toString('utf8')
}

function vaultWrite() {
  const method = vaultMethod()
  const payload = JSON.stringify(vaultCache || [])
  try {
    if (method === 'master') {
      if (!vaultKey) return { ok: false, reason: 'vault is locked' }
      fs.writeFileSync(path.join(USER, VAULT_FILE), vaultSeal(payload, vaultKey))
    } else if (method === 'keychain') {
      fs.writeFileSync(path.join(USER, VAULT_FILE), safeStorage.encryptString(payload))
    } else {
      return { ok: false, reason: 'no encryption available - the vault will not write to disk unencrypted' }
    }
    return { ok: true, method }
  } catch (e) { return { ok: false, reason: e.message } }
}

function vaultLoad() {
  const f = path.join(USER, VAULT_FILE)
  if (!fs.existsSync(f)) return []
  const method = vaultMethod()
  try {
    if (method === 'master') {
      if (!vaultKey) return null
      return JSON.parse(vaultOpen(fs.readFileSync(f), vaultKey))
    }
    if (method === 'keychain') return JSON.parse(safeStorage.decryptString(fs.readFileSync(f)))
  } catch { return null }
  return null
}

function vaultArmLock() {
  clearTimeout(vaultTimer)
  const min = Number(settings.vaultLockMin) || 0
  if (min > 0) {
    vaultTimer = setTimeout(() => {
      vaultLocked = true
      vaultCache = null
      vaultKey = null
      vaultBroadcast()
    }, min * 60000)
  }
}

function vaultBroadcast() {
  for (const w of mainWindows) { try { w.sendChrome('vault', vaultStatus()) } catch (e) { /* window gone */ } }
}

function vaultStatus() {
  const hasFile = fs.existsSync(path.join(USER, VAULT_FILE))
  return {
    enabled: settings.vaultEnabled !== false,
    method: vaultMethod(),
    locked: vaultLocked && !vaultKey,
    hasFile,
    count: vaultLocked ? 0 : (vaultCache ? vaultCache.length : 0),
    lockMin: Number(settings.vaultLockMin) || 0,
    // true when a store would be safe right now
    canStore: vaultMethod() !== 'none'
  }
}

function vaultUnlock(pw) {
  if (vaultMethod() !== 'master') {
    vaultCache = vaultLoad() || []
    vaultLocked = false
    vaultArmLock()
    return { ok: true }
  }
  const meta = readJSON(VAULT_KEY, null)
  if (!meta) return { ok: false, reason: 'no master password is set' }
  let key
  try { key = vaultDerive(pw, meta.salt) } catch (e) { return { ok: false, reason: e.message } }
  try {
    const check = JSON.parse(vaultOpen(Buffer.from(meta.check, 'base64'), key))
    if (check.v !== 1) throw new Error('bad verifier')
  } catch { return { ok: false, reason: 'wrong master password' } }
  vaultKey = key
  vaultCache = vaultLoad() || []
  vaultLocked = false
  vaultArmLock()
  return { ok: true }
}

const VAULT_WORDS = ('anchor atlas beacon branch candle canyon cedar cinder cobalt copper coral cradle crater crimson cubic cypress daisy delta dimple dynamo ember emberly fathom flint forest garnet glacier granite harbor heron indigo ivory jasper juniper kestrel lantern lattice lighthouse lotus lumen maple marble meadow mesa nebula nickel oak obsidian onyx opal orion pebble pine pixel plaza quartz quill raven reef ridge rocket rose ruby saffron sage coral slate solstice sparrow spruce steel summit sylvan tamarind delta timber topaz tundra umber valley velvet vertex violet walnut willow zephyr zenith basalt beech birch bluffs cactus canyon crest dune fern fjord gully heath inlet jaguar lago moor oasis peak plateau quarry ridge savanna shelf sierra slope tidal tundra vista warden'
  ).split(/\s+/).filter(Boolean)

function vaultGenerate(opts) {
  const o = opts || {}
  const mode = o.mode || 'words'
  if (mode === 'chars') {
    const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*-_+='
    const len = Math.min(64, Math.max(8, Number(o.length) || 20))
    let out = ''
    const buf = new Uint32Array(len)
    crypto.randomFillSync(buf)
    for (let i = 0; i < len; i++) out += alphabet[buf[i] % alphabet.length]
    return out
  }
  const n = Math.min(8, Math.max(3, Number(o.words) || 4))
  const buf = new Uint32Array(n + 1)
  crypto.randomFillSync(buf)
  const words = []
  for (let i = 0; i < n; i++) words.push(VAULT_WORDS[buf[i] % VAULT_WORDS.length])
  const cap = words.map((w, i) => (i === 0 ? w[0].toUpperCase() + w.slice(1) : w))
  return cap.join('-') + '-' + (10 + (buf[n] % 90))
}

// A saved entry may only be offered on its own host or a subdomain of it - and,
// the other way round, an entry saved for www.github.com may fill github.com.
// The previous version compared "last two labels", which meant an entry stored
// for github.co.uk was offered on any *.co.uk site. That is a cross-site password
// suggestion, so it is gone: equality of the registrable domain is a necessary
// condition, a host suffix match is the sufficient one.
function siteHost(site) {
  try { return new URL(/^https?:/.test(site) ? site : 'https://' + site).hostname.toLowerCase() }
  catch { return '' }
}

function vaultMatching(host) {
  if (!host || vaultLocked || !vaultCache) return []
  const h = String(host).toLowerCase()
  return vaultCache.filter((e) => {
    const s = siteHost(e.site)
    if (!s || etld1(s) !== etld1(h)) return false
    return s === h || h.endsWith('.' + s) || s.endsWith('.' + h)
  })
}

// Credentials must never be offered to a page whose top-level origin differs from
// the frame the field lives in (a login iframe on someone else's page).
function vaultFrameAllowed(tabUrl, frameUrl) {
  if (!frameUrl || /^about:|^data:|^file:/.test(frameUrl)) return true
  try { return new URL(frameUrl).origin === new URL(tabUrl).origin }
  catch { return false }
}

// Nothing that looks like inline credentials may be written to history or the
// session file: https://user:pass@host is a real URL form.
function storeUrl(u) {
  try {
    const x = new URL(u)
    if (x.username || x.password) { x.username = ''; x.password = '' }
    return x.href
  } catch { return String(u || '') }
}

// Read the form the user is standing in, without asking them to type anything.
const VAULT_GRAB = `(function () {
  const el = document.activeElement
  if (!el || !el.tagName) return null
  const form = el.form || el.closest('form') || document
  const pwEl = (el.type === 'password') ? el : form.querySelector('input[type=password]')
  if (!pwEl) return { error: 'no password field here' }
  const inputs = [...form.querySelectorAll('input')].filter(i => i.type !== 'password' && i.type !== 'hidden' && i.type !== 'submit')
  const guess = inputs.find(i => /(user|login|email|account|name)/i.test((i.name || '') + (i.id || '') + (i.placeholder || '')))
  const value = (node) => { try { return node.value || '' } catch { return '' } }
  return { user: guess ? value(guess) : (inputs[0] ? value(inputs[0]) : ''), pass: value(pwEl) }
})()`

const VAULT_FILL = `(function (payload) {
  const el = document.activeElement
  const form = (el && (el.form || el.closest('form'))) || document.querySelector('form') || document
  let done = []
  if (payload.user) {
    const inputs = [...form.querySelectorAll('input')].filter(i => i.type !== 'password' && i.type !== 'hidden')
    const target = inputs.find(i => /(user|login|email|account|name)/i.test((i.name || '') + (i.id || '') + (i.placeholder || ''))) || inputs[0]
    if (target) { target.value = payload.user; done.push('user') }
  }
  const pw = form.querySelector('input[type=password]')
  if (pw) { pw.value = payload.pass; done.push('password') }
  for (const i of form.querySelectorAll('input')) i.dispatchEvent(new Event('input', { bubbles: true }))
  return done.join('+') || 'nothing filled'
})`

// ---------------------------------------------------------------- workspaces
// A workspace is simply the set of tabs in the window. Switching saves one and
// loads another, which is what GX's workspace strip does.
let workspaces = readJSON('workspaces.json', null) || { active: 0, list: [{ name: 'General' }] }
if (!workspaces.list || !workspaces.list.length) workspaces.list = [{ name: 'General' }]
for (const ws of workspaces.list) if (!ws.urls) ws.urls = []
if (workspaces.active >= workspaces.list.length) workspaces.active = 0

function saveWorkspaces() { writeJSON('workspaces.json', workspaces) }
function wsPayload() {
  return {
    active: workspaces.active,
    list: workspaces.list.map((w) => ({ name: w.name || 'Workspace', count: (w.urls || []).length }))
  }
}
function wsBroadcast() {
  for (const w of mainWindows) { try { w.sendChrome('workspaces', wsPayload()) } catch (e) { /* gone */ } }
}

// RAM limiter: tabs idle past the threshold are terminated, not closed. The pill
// stays in the strip and the page reloads when you click it.
function discardIdleTabs() {
  const limit = sleepLimitMinutes() * 60000
  if (!limit) return
  for (const w of mainWindows) {
    let changed = false
    for (const [id, t] of w.tabs) {
      if (id === w.activeId || id === w.splitId || t.pinned || t.discarded) continue
      if (!/^https?:/.test(t.url || '')) continue
      if (Date.now() - (t.lastActive || 0) < limit) continue
      // Electron 45 has no webContents.terminate(); unloading the page to
      // about:blank releases the same renderer memory and keeps the pill alive.
      try {
        t.discardUrl = t.url
        t.discarded = true
        t.loading = false
        t.view.webContents.loadURL('about:blank')
        changed = true
      } catch (e) { t.discarded = false }
    }
    if (changed) w.sendState()
  }
}


// ---------------------------------------------------------------- search
const ENGINES = {
  duckduckgo: { name: 'DuckDuckGo', url: 'https://duckduckgo.com/?q=' },
  brave:      { name: 'Brave Search', url: 'https://search.brave.com/search?q=' },
  startpage:  { name: 'Startpage', url: 'https://www.startpage.com/sp/search?query=' },
  wikipedia:  { name: 'Wikipedia', url: 'https://en.wikipedia.org/w/index.php?search=' }
}

function looksLikeURL(s) {
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(s)) return true
  if (/^localhost(:\d+)?(\/|$)/i.test(s)) return true
  if (/^\d{1,3}(\.\d{1,3}){3}(:\d+)?(\/|$)/.test(s)) return true
  // bare domain with a known-ish TLD or a dot and no spaces
  if (!/\s/.test(s) && /^[^\s/]+\.[a-z]{2,}([/:?#].*)?$/i.test(s)) return true
  return false
}

function normalize(input) {
  const s = (input || '').trim()
  if (!s) return settings.homepage
  if (s === 'psychovault://newtab' || s === 'about:newtab') return 'psychovault://newtab'
  if (s === 'psychovault://settings' || s === 'about:settings') return 'psychovault://settings'
  if (looksLikeURL(s)) {
    if (/^[a-z][a-z0-9+.-]*:\/\//i.test(s)) return s
    return 'https://' + s
  }
  const eng = ENGINES[settings.search] || ENGINES.duckduckgo
  return eng.url + encodeURIComponent(s)
}

// ---------------------------------------------------------------- windows
const mainWindows = new Set()
let lastClosedTabs = []

class TabWindow {
  constructor(opts = {}) {
    this.private = !!opts.private
    this.tabs = new Map()
    this.order = []
    this.activeId = null
    this.splitId = null
    this.extraTop = 0
    this.panelOpen = false
    this.chromeModal = false
    this.nextId = 1
    this.findActive = false

    const wsSet = (!this.private && settings.restore) ? workspaces.list[workspaces.active] : null
    const saved = (wsSet && wsSet.urls && wsSet.urls.length) ? wsSet
      : ((!this.private && settings.restore) ? readJSON('session.json', null) : null)

    this.win = new BrowserWindow({
      width: 1400,
      height: 900,
      minWidth: 700,
      minHeight: 420,
      frame: false,
      show: false,
      backgroundColor: '#0a0e17',
      title: 'PsychoVault',
      icon: path.join(__dirname, 'ui', 'icon.png'),
      webPreferences: {
        preload: path.join(__dirname, 'preload.js'),
        contextIsolation: true,
        nodeIntegration: false,
        sandbox: true,
        spellcheck: false       // red lines in the address bar would be silly
      }
    })

    this.win.loadFile(path.join(__dirname, 'ui', 'index.html'), {
      query: { private: this.private ? '1' : '0' }
    })

    this.win.webContents.on('context-menu', (_e, params) => this.showChromeMenu(params))
    this.win.once('ready-to-show', () => this.win.show())

    // A frameless window on Windows fires resize bursts while it is being dragged,
    // and some of those bursts report a transient client height. Laying out from a
    // transient value collapses the page views and nothing brings them back, so the
    // drag is followed by a couple of debounced passes instead of an immediate one.
    const relayout = () => this.layoutSoon()
    this.win.on('resize', relayout)
    this.win.on('moved', relayout)
    this.win.on('focus', relayout)
    this.win.on('show', relayout)
    this.win.on('restore', () => { relayout(); this.sendState() })
    this.win.on('minimize', relayout)
    // State pushes are full snapshots; a partial `{ maximized }` object used to be
    // treated as a complete tab state by the renderer and erased the visible tabs.
    this.win.on('maximize', () => { relayout(); this.sendState() })
    this.win.on('unmaximize', () => { relayout(); this.sendState() })
    this.win.on('enter-full-screen', () => this.layout())
    this.win.on('leave-full-screen', () => this.layout())
    this.win.on('closed', () => {
      mainWindows.delete(this)
      statWindows.delete(this)
      for (const t of this.tabs.values()) wcTabs.delete(t.wcId)
      this.tabs.clear()
    })
    statWindows.add(this)

    this.loadBlocklistOnce()

    this.win.webContents.once('did-finish-load', () => {
      if (saved && saved.urls && saved.urls.length) {
        for (const [i, u] of saved.urls.entries()) {
          const f = (saved.flags && saved.flags[i]) || {}
          this.createTab(u, { activate: false, pinned: !!f.pinned })
          if (f.muted) this.setMuted(this.order[this.order.length - 1], true)
        }
        this.activate(this.order[Math.min(saved.active || 0, this.order.length - 1)])
      } else {
        this.createTab(settings.homepage)
      }
      this.sendState()
    })

    // Links that ask for a new window become tabs
    this.win.webContents.setWindowOpenHandler(({ url }) => {
      if (url.startsWith('http')) this.createTab(url)
      return { action: 'deny' }
    })
  }

  loadBlocklistOnce() {
    if (this.blocklistReady) return
    this.blocklistReady = true
    loadBlocklist()
  }

  // ------------------------------------------------------------ tabs
  newSession(container) {
    const partition = this.private ? `pv-private-${Date.now()}`
      : (container ? `persist:pv-c-${container.toLowerCase()}` : 'persist:psychovault')
    const ses = session.fromPartition(partition)
    this.installBlocking(ses)
    applySessionPolicy(ses)
    watchDownloads(ses)
    trackSession(ses)
    return ses
  }

  installBlocking(ses) {
    if (ses._pvBlocked) return
    ses._pvBlocked = true
    ses.webRequest.onBeforeRequest((details, cb) => {
      if (isBlocked(details.url)) {
        noteBlocked(details.webContentsId)
        return cb({ cancel: true })
      }
      cb({})
    })
    ses.setPermissionRequestHandler((wc, permission, cb, details) => {
      if (permDenied(permission)) return cb(false)
      cb(true, details && details.requestingUrl ? { securityOrigin: details.requestingUrl } : undefined)
    })
    ses.setPermissionCheckHandler((wc, permission) => !permDenied(permission))
  }

  createTab(url, opts = {}) {
    const id = this.nextId++
    const containerName = this.private ? null : (opts.container || null)
    const ses = this.newSession(containerName)

    const view = new WebContentsView({
      webPreferences: {
        session: ses,
        preload: path.join(__dirname, 'page-preload.js'),
        contextIsolation: true,
        nodeIntegration: false,
        sandbox: true,
        webviewTag: false,
        spellcheck: true
      }
    })

    const tab = {
      id,
      view,
      title: 'New Tab',
      url: '',
      favicon: null,
      loading: false,
      canGoBack: false,
      canGoForward: false,
      blocked: 0,
      pinned: !!opts.pinned,
      muted: false,
      discarded: false,
      lastActive: Date.now(),
      container: containerName,
      island: opts.parentId ? (this.tabs.get(opts.parentId)?.island || opts.parentId) : null,
      service: !!opts.service
    }
    this.tabs.set(id, tab)
    this.order.push(id)
    this.win.contentView.addChildView(view)
    view.setVisible(false)

    const wc = view.webContents
    tab.wcId = wc.id
    wcTabs.set(wc.id, tab)

    const emit = () => this.sendState()
    wc.on('dom-ready', () => applySpellToPage(wc))
    wc.on('did-start-navigation', (_e, url, isInPlace, isMainFrame) => {
      if (isMainFrame && !isInPlace) wc._pvSpellSeeded = false
    })

    wc.on('page-title-updated', (_e, title) => {
      if (tab.discarded) return
      if (!title) return
      tab.title = title
      if (!this.private && /^https?:/.test(tab.url)) {
        const h = history.find(x => x.url === tab.url)
        if (h && h.title !== title) { h.title = title; writeJSON('history.json', history) }
      }
      emit()
    })
    wc.on('page-favicon-updated', (_e, icons) => { if (icons && icons.length) { tab.favicon = icons[0]; emit() } })
    wc.on('did-start-loading', () => { tab.loading = true; emit() })
    wc.on('did-stop-loading', () => {
      tab.loading = false
      tab.canGoBack = wc.navigationHistory ? wc.navigationHistory.canGoBack() : wc.canGoBack()
      tab.canGoForward = wc.navigationHistory ? wc.navigationHistory.canGoForward() : wc.canGoForward()
      this.applyEnhancements(tab)
      emit()
    })
    wc.on('did-navigate', (_e, u) => {
      tab.blocked = 0
      // the unload that a sleeping tab performs is ours, not the user's
      if (u === 'about:blank' && tab.discarded) { emit(); return }
      tab.url = u
      this.recordHistory(u, tab.title)
      emit()
    })
    wc.on('did-navigate-in-page', (_e, u, isMain) => {
      if (isMain) { tab.url = u; emit() }
    })
    wc.on('update-target-url', (_e, u) => {
      if (this.activeId === id) this.sendChrome('hover', { url: u })
    })
    wc.on('did-fail-load', (_e, code, desc, u, isMain) => {
      if (!isMain || code === -3) return
      this.sendChrome('loadfail', { code, desc, url: u })
    })
    wc.on('render-process-gone', () => {
      tab.title = 'Page crashed'
      tab.loading = false
      emit()
    })

    wc.setWindowOpenHandler(({ url: target }) => {
      // remember who opened it: that is what makes a GX tab island collapsible
      if (/^http/.test(target)) { this.createTab(target, { parentId: tab.id, container: tab.container }); return }
      if (target && !target.startsWith('javascript:')) this.createTab(target)
      return { action: 'deny' }
    })

    wc.on('context-menu', (_e, params) => this.showPageMenu(params, tab))
    this.bindPageShortcuts(wc, tab)

    this.go(tab, url || 'psychovault://newtab')
    if (opts.activate !== false) this.activate(id)
    else view.setVisible(false)

    return tab
  }

  // Keyboard works even when the page itself has focus. UI-only commands are
  // forwarded to the chrome renderer; navigation commands are handled here.
  bindPageShortcuts(wc, tab) {
    wc.on('before-input-event', (event, input) => {
      if (input.type !== 'keyDown') return
      const ctrl = input.control || input.meta
      const key = (input.key || '').toLowerCase()
      const stop = () => event.preventDefault()

      if (!ctrl) {
        if (input.key === 'F5') { stop(); wc.reload() }
        else if (input.key === 'F12' && settings.panicEnabled) { stop(); this.panic(true) }
        else if (input.key === 'F11' && this._panicked) { stop(); this.panic(false) }
        return
      }
      if (key === 'k') { stop(); this.sendChrome('cmd', 'palette') }
      else if (key === 'l') { stop(); this.sendChrome('cmd', 'omnibox') }
      else if (key === 'f' && !input.shift) { stop(); this.sendChrome('cmd', 'find') }
      else if (key === 'r') { stop(); wc.reload() }
      else if (key === 't' && input.shift) { stop(); this.reopenClosed() }
      else if (key === 't') { stop(); this.createTab(settings.homepage) }
      else if (key === 'w') { stop(); this.closeTab(tab.id) }
      else if (key === 'p' && input.shift) {
        stop()
        const w = new TabWindow({ private: true })
        mainWindows.add(w)
      }
      else if (key === 'd') { stop(); this.sendChrome('cmd', 'bookmark') }
      else if (key === '5' && input.shift) { stop(); this.capturePage('png').then((r) => this.sendChrome('vault', { toast: r.ok ? 'Snapshot saved to ' + r.file : 'Snapshot failed: ' + r.reason })) }
      else if (/^[1-9]$/.test(key)) { stop(); this.wsSwitch(Number(key) - 1) }
      else if (key === '\\') { stop(); this.toggleSplit() }
      else if (key === '=' || key === '+') { stop(); wc.setZoomLevel(wc.getZoomLevel() + 0.5) }
      else if (key === '-') { stop(); wc.setZoomLevel(wc.getZoomLevel() - 0.5) }
      else if (key === '0') { stop(); wc.setZoomLevel(0) }
      else if (key === 'tab') { /* let the page keep tab handling */ }
    })
  }

  go(tab, url) {
    const target = normalize(url)
    tab.url = target
    if (target === 'psychovault://newtab') {
      tab.view.webContents.loadFile(path.join(__dirname, 'ui', 'newtab.html'), { query: { private: this.private ? '1' : '0' } })
    } else if (target === 'psychovault://settings') {
      tab.view.webContents.loadFile(path.join(__dirname, 'ui', 'settings.html'))
    } else {
      tab.view.webContents.loadURL(target).catch(() => {})
    }
    this.sendState()
  }

  activate(id) {
    if (!this.tabs.has(id)) return
    const now = this.tabs.get(id)
    if (this.splitId) this.revive(this.tabs.get(this.splitId))
    if (now) {
      now.lastActive = Date.now()
      this.revive(now)
    }
    if (this.splitId && id === this.splitId) {
      // picking the side pane promotes it and demotes the old main pane
      this.splitId = this.activeId
    }
    this.activeId = id
    for (const [tid, t] of this.tabs) {
      t.view.setVisible(tid === id || tid === this.splitId)
    }
    this.layout()
    this.sendState()
    const t = this.tabs.get(id)
    if (t) t.view.webContents.focus()
  }

  // Bring a sleeping tab back: reload the URL it was dropped from.
  revive(x) {
    // called with an id from the tab menu and the GX panel, with a tab object from
    // the activation path - a number is not a tab, so resolve it here
    const t = typeof x === 'object' ? x : this.tabs.get(x)
    if (!t || !t.discarded) return
    t.discarded = false
    t.loading = true
    const url = t.discardUrl || t.url
    t.discardUrl = null
    try { t.view.webContents.loadURL(url) } catch (e) { /* view gone */ }
    this.sendState()
  }

  setPinned(id, pinned) {
    const t = this.tabs.get(id)
    if (!t) return
    t.pinned = !!pinned
    // pinned tabs live at the head of the strip; sort is stable so order is kept
    this.order = this.order.sort((a, b) => (this.tabs.get(b)?.pinned ? 1 : 0) - (this.tabs.get(a)?.pinned ? 1 : 0))
    this.layout()
    this.sendState()
  }

  // Reorder the strip. The renderer says "put this tab before that id", not an index:
  // indices in the DOM skip collapsed islands, so an id is the only stable anchor.
  // Pinned tabs are one block at the head - a drag that would cross that line stops at it.
  moveTab(id, beforeId) {
    const t = this.tabs.get(id)
    if (!t || !this.order.includes(id)) return
    if (beforeId === id) return
    const rest = this.order.filter((x) => x !== id)
    let at = rest.length
    if (beforeId) {
      const i = rest.indexOf(beforeId)
      if (i >= 0) at = i
      else if (!this.tabs.has(beforeId)) at = rest.length   // stale id from a closed tab
    }
    const pinnedEnd = rest.filter((x) => this.tabs.get(x) && this.tabs.get(x).pinned).length
    at = t.pinned ? Math.max(0, Math.min(pinnedEnd, at)) : Math.max(pinnedEnd, Math.min(rest.length, at))
    rest.splice(at, 0, id)
    if (rest.join(',') === this.order.join(',')) return   // dropped where it already was
    this.order = rest
    this.layout()
    this.sendState()
    this.saveSession()          // order is part of the session, so it survives a restart
  }

  setMuted(id, muted) {
    const t = this.tabs.get(id)
    if (!t) return
    t.muted = !!muted
    try { t.view.webContents.setAudioMuted(t.muted) } catch (e) { /* gone */ }
    this.sendState()
  }

  duplicateTab(id) {
    const t = this.tabs.get(id)
    if (!t) return
    this.createTab(/^https?:/.test(t.url) ? t.url : 'psychovault://newtab')
  }

  closeOtherTabs(id) {
    for (const other of [...this.order]) {
      if (other === id) continue
      const t = this.tabs.get(other)
      if (!t || t.pinned) continue
      this.closeTab(other)
    }
    if (this.tabs.has(id)) this.activate(id)
  }

  closeTabsToRight(id) {
    const i = this.order.indexOf(id)
    if (i < 0) return
    for (const other of this.order.slice(i + 1)) {
      const t = this.tabs.get(other)
      if (!t || t.pinned) continue
      this.closeTab(other)
    }
  }

  moveTabToNewWindow(id) {
    const t = this.tabs.get(id)
    if (!t) return
    const url = t.url
    const w = new TabWindow({ private: this.private })
    mainWindows.add(w)
    w.win.webContents.once('did-finish-load', () => {
      w.createTab(/^https?:/.test(url) ? url : settings.homepage)
      this.closeTab(id)
    })
  }

  vaultMenuItems(tab, params) {
    if (settings.vaultEnabled === false) return []
    const site = /^https?:/.test(tab.url || '') ? hostOf(tab.url) : ''
    if (!site) return []
    if (params && params.frameURL && !vaultFrameAllowed(tab.url, params.frameURL)) {
      return [{ label: 'This field is in a frame from another site - the vault will not touch it', enabled: false }]
    }
    const out = []
    for (const m of vaultMatching(site).slice(0, 5)) {
      out.push({
        label: `Fill ${m.user} @ ${m.site}`,
        click: () => {
          tab.view.webContents
            .executeJavaScript(`${VAULT_FILL}(${JSON.stringify({ user: m.user, pass: m.pass })})`)
            .then((how) => this.sendChrome('vault', { ...vaultStatus(), toast: 'Filled ' + how }))
            .catch(() => this.sendChrome('vault', { ...vaultStatus(), toast: 'Fill failed - this page blocked the script' }))
        }
      })
    }
    if (vaultLocked) {
      out.push({ label: 'Vault is locked - open Settings to unlock it', click: () => this.createTab('psychovault://settings', { activate: true }) })
      return out
    }
    out.push({
      label: 'Save password for this site',
      click: () => {
        tab.view.webContents.executeJavaScript(VAULT_GRAB).then((got) => {
          if (!got || got.error || !got.pass) {
            this.sendChrome('vault', { ...vaultStatus(), toast: (got && got.error) || 'This form has no password value to read' })
            return
          }
          const entry = { id: crypto.randomUUID(), site, user: got.user || '', pass: got.pass, note: '', at: Date.now() }
          const dup = vaultCache.findIndex((e) => e.site === site && e.user === entry.user)
          if (dup >= 0) vaultCache[dup] = { ...vaultCache[dup], ...entry }
          else vaultCache.push(entry)
          const w = vaultWrite()
          if (!w.ok) vaultCache.splice(dup >= 0 ? dup : vaultCache.length - 1, 1)
          vaultArmLock()
          this.sendChrome('vault', { ...vaultStatus(), toast: w.ok ? (dup >= 0 ? 'Password updated in the vault' : 'Password saved to the vault') : w.reason })
        }).catch(() => this.sendChrome('vault', { ...vaultStatus(), toast: 'This page blocked reading its form' }))
      }
    })
    return out
  }

  // RGX mode: the same two switches GX ships, done as CSS filters on the page.
  static ENHANCE_CSS = `(() => {
    const v = __VIDEO__, i = __IMAGES__
    let css = ''
    if (v) css += 'video{filter:contrast(1.06) saturate(1.10) brightness(1.03)}'
    if (i) css += 'img{filter:contrast(1.04) saturate(1.06)}'
    const node = document.getElementById('__pv_rgx')
    if (css && !node) {
      const st = document.createElement('style')
      st.id = '__pv_rgx'
      st.textContent = css
      document.documentElement.appendChild(st)
    } else if (!css && node) node.remove()
    else if (css && node) node.textContent = css
    return !!css
  })()`

  static ACCESSIBILITY_CSS = `(() => {
    const on = __REDUCE__
    const id = '__pv_reduce_motion'
    let style = document.getElementById(id)
    if (on && !style) {
      style = document.createElement('style')
      style.id = id
      document.documentElement.appendChild(style)
    }
    if (style && on) style.textContent = '*,:before,:after{scroll-behavior:auto!important;animation-duration:.01ms!important;animation-iteration-count:1!important;transition-duration:.01ms!important}'
    else if (style && !on) style.remove()
    return on
  })()`

  applyEnhancements(tab) {
    const wc = tab && tab.view && tab.view.webContents
    if (!wc || wc.isDestroyed()) return
    if (!/^https?:/.test(tab.url || '')) return
    const on = !!settings.enhanceVideo || !!settings.enhanceImages
    if (!on) {
      wc.executeJavaScript(`(function(){const a=document.getElementById('__pv_rgx');if(a)a.remove();return true})()`).catch(() => {})
    } else {
      const css = TabWindow.ENHANCE_CSS
        .replace('__VIDEO__', JSON.stringify(!!settings.enhanceVideo))
        .replace('__IMAGES__', JSON.stringify(!!settings.enhanceImages))
      wc.executeJavaScript(css).catch(() => { /* page refused; enhancement is cosmetic */ })
    }
    const accessibilityCss = TabWindow.ACCESSIBILITY_CSS.replace('__REDUCE__', JSON.stringify(!!settings.reduceMotion))
    wc.executeJavaScript(accessibilityCss).catch(() => { /* the page may have navigated away */ })
  }

  setEnhancements(patch) {
    settings = { ...settings, ...patch }
    writeJSON('settings.json', settings)
    for (const w of mainWindows) for (const t of w.tabs.values()) w.applyEnhancements(t)
    for (const w of mainWindows) w.sendState()
    return settings
  }

  // GX Panic Button: tuck every tab away and show a bland page.
  panic(on) {
    if (on === this._panicked) return { panicked: !!this._panicked, held: this._panicStack ? this._panicStack.length : 0 }
    if (on) {
      this._panicStack = this.order.map((id) => {
        const t = this.tabs.get(id)
        return t ? { url: t.url, pinned: !!t.pinned } : null
      }).filter(Boolean)
      for (const id of [...this.order]) this.closeTab(id, { keepOpen: true })
      this.createTab(settings.panicUrl || 'https://en.wikipedia.org/wiki/Main_Page', { activate: true })
      this._panicked = true
    } else {
      const stack = this._panicStack || []
      this._panicked = false
      this._panicStack = null
      for (const id of [...this.order]) this.closeTab(id, { keepOpen: true })
      for (const t of stack) this.createTab(t.url, { activate: false, pinned: t.pinned })
      if (!this.order.length) this.createTab(settings.homepage)
      this.activate(this.order[this.order.length - 1])
    }
    this.sendState()
    return { panicked: this._panicked, held: stack0(this) }
    function stack0(w) { return w._panicStack ? w._panicStack.length : 0 }
  }

  hotTabs() {
    let metrics = []
    try { metrics = app.getAppMetrics() } catch { metrics = [] }
    const byPid = new Map(metrics.map((m) => [m.pid, m]))
    const rows = []
    for (const id of this.order) {
      const t = this.tabs.get(id)
      if (!t) continue
      let pid = 0
      try { pid = t.view.webContents.getOSProcessId() } catch { pid = 0 }
      const m = byPid.get(pid)
      rows.push({
        id,
        title: t.title || 'Untitled',
        url: t.url || '',
        memMB: m ? Math.round((m.memory.workingSetSize || 0) / 1024) : 0,
        cpu: m ? Math.round((m.cpu.percentCPUUsage || 0) * 10) / 10 : 0,
        audible: safe(() => t.view.webContents.isCurrentlyAudible(), false),
        discarded: !!t.discarded,
        active: id === this.activeId
      })
    }
    return rows.sort((a, b) => b.memMB - a.memMB)
  }

  mediaRoute(cmd) {
    const t = this.tabs.get(this.activeId) || [...this.tabs.values()].find((x) => x.service) || [...this.tabs.values()][0]
    if (!t) return { ok: false, reason: 'no tab to control' }
    const js = {
      play: `document.querySelectorAll('audio,video').forEach(m=>{try{m.play().catch(()=>{})}catch(e){/* media may be detached or policy-blocked */}});'play'`,
      pause: `document.querySelectorAll('audio,video').forEach(m=>{try{m.pause()}catch(e){/* media may be detached */}});'pause'`,
      toggle: `(()=>{const m=document.querySelector('audio,video');if(!m)return 'none';if(m.paused){try{m.play().catch(()=>{})}catch(e){/* media may be detached or policy-blocked */}}else m.pause();return m.paused?'pause':'play'})()`,
      next: `[...document.querySelectorAll('audio,video')].forEach(m=>{try{m.currentTime=m.duration-1}catch(e){/* live or unseekable media */}});'next'`,
      prev: `[...document.querySelectorAll('audio,video')].forEach(m=>{try{m.currentTime=0}catch(e){/* live or unseekable media */}});'prev'`
    }[cmd]
    if (!js) return { ok: false, reason: 'unknown command ' + cmd }
    return t.view.webContents.executeJavaScript(js)
      .then((how) => ({ ok: true, how, tab: t.id }))
      .catch((e) => ({ ok: false, reason: e.message }))
  }

  capturePage(kind) {
    const t = this.tabs.get(this.activeId)
    if (!t) return { ok: false, reason: 'no active tab' }
    const dir = app.getPath('downloads')
    try { fs.mkdirSync(dir, { recursive: true }) } catch { /* exists */ }
    const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
    if (kind === 'pdf') {
      return t.view.webContents.printToPDF({ printBackground: true, pageSize: 'A4' })
        .then((buf) => {
          const file = path.join(dir, `psychovault-${stamp}.pdf`)
          fs.writeFileSync(file, buf)
          return { ok: true, file, bytes: buf.length }
        }).catch((e) => ({ ok: false, reason: e.message }))
    }
    return t.view.webContents.capturePage().then((img) => {
      const buf = img.toPNG()
      const file = path.join(dir, `psychovault-${stamp}.png`)
      fs.writeFileSync(file, buf)
      return { ok: true, file, bytes: buf.length }
    }).catch((e) => ({ ok: false, reason: e.message }))
  }

  setContainer(id, name) {
    const t = this.tabs.get(id)
    if (!t || this.private) return
    if ((t.container || null) === name) return
    const url = t.url
    // Different partition = different cookie jar, localStorage and cache: the
    // page has to be reloaded into it, the same as Firefox containers do.
    this.closeTab(id, { keepOpen: true })
    const nt = this.createTab(url, { container: name, activate: true })
    if (name) this.sendChrome('vault', { toast: 'Reopened in ' + name + ' - signed in separately' })
    this.saveSession()
  }

  discardTab(id) {
    const t = this.tabs.get(id)
    if (!t || t.discarded || !/^https?:/.test(t.url || '')) return
    t.discardUrl = t.url
    t.discarded = true
    t.view.webContents.loadURL('about:blank').catch(() => {})
    this.sendState()
  }

  searchSelection(text) {
    const eng = ENGINES[settings.search] || ENGINES.duckduckgo
    this.createTab(eng.url + encodeURIComponent(text))
  }

  // Right-click on the chrome itself (address bar, search box): same set of edit
  // commands, plus paste-and-go so a copied link can be opened without typing.
  showChromeMenu(params) {
    const wc = this.win.webContents
    const items = []
    if (params.isEditable) {
      const f = params.editFlags || {}
      items.push({ label: 'Cut', enabled: !!f.canCut, click: () => wc.cut() })
      items.push({ label: 'Copy', enabled: !!f.canCopy, click: () => wc.copy() })
      items.push({ label: 'Paste', enabled: f.canPaste !== false, click: () => wc.paste() })
      items.push({ label: 'Select all', enabled: !!f.canSelectAll, click: () => wc.selectAll() })
      items.push({ type: 'separator' })
      items.push({
        label: 'Paste and go',
        // the clipboard is read when you choose the item, not while the menu is built:
        // an async clipboard cannot be inspected to decide whether the item is greyed out
        click: async () => {
          const text = await clipText()
          const t = this.tabs.get(this.activeId)
          if (!text) { this.sendChrome('vault', { toast: 'The clipboard is empty' }); return }
          if (t) this.go(t, text)
        }
      })
    } else {
      items.push({ label: 'Back', enabled: (this.tabs.get(this.activeId) || {}).canGoBack, click: () => wc.goBack() })
      items.push({ label: 'Reload', click: () => wc.reload() })
    }
    Menu.buildFromTemplate(items).popup({ window: this.win })
  }

  showTabMenu(id, point) {
    const t = this.tabs.get(id)
    if (!t) return
    const menu = Menu.buildFromTemplate(this.tabMenuTemplate(t))
    if (point && Number.isFinite(point.x) && Number.isFinite(point.y)) {
      menu.popup({ window: this.win, x: Math.round(point.x), y: Math.round(point.y), callback: () => this.sendState() })
    } else {
      menu.popup({ window: this.win, callback: () => this.sendState() })
    }
  }

  tabMenuTemplate(t) {
    const id = t.id
    const http = /^https?:/.test(t.url || '')
    return [
      { label: 'Open new tab to the right', click: () => this.createTab('psychovault://newtab') },
      { label: 'Reload', enabled: http, click: () => this.go(t, t.url) },
      { label: 'Duplicate tab', click: () => this.duplicateTab(id) },
      { type: 'separator' },
      { label: t.pinned ? 'Unpin tab' : 'Pin tab', click: () => this.setPinned(id, !t.pinned) },
      { label: t.muted ? 'Unmute tab' : 'Mute tab', click: () => this.setMuted(id, !t.muted) },
      {
        label: this.splitId === id ? 'Close split view' : 'Open in split view',
        enabled: this.order.length > 1,
        click: () => { if (this.activeId !== id) this.activate(id); this.toggleSplit() }
      },
      { type: 'separator' },
      { label: 'Move tab to new window', enabled: this.order.length > 1 || http, click: () => this.moveTabToNewWindow(id) },
      { label: 'Copy tab URL', enabled: http, click: () => clipboard.writeText(t.url) },
      {
        label: t.discarded ? 'Wake this tab' : 'Sleep this tab now',
        click: () => { if (!t.discarded) this.discardTab(id); else this.revive(id); this.sendState() }
      },
      { label: 'Capture visible page (Snapshot)', enabled: http, click: () => this.capturePage('png').then((r) => this.sendChrome('vault', { toast: r.ok ? 'Snapshot saved to ' + r.file : 'Snapshot failed: ' + r.reason })) },
      { label: 'Save page as PDF', enabled: http, click: () => this.capturePage('pdf').then((r) => this.sendChrome('vault', { toast: r.ok ? 'PDF saved to ' + r.file : 'PDF failed: ' + r.reason })) },
      {
        label: 'Container',
        submenu: [
          { label: 'No container (default profile)', type: 'checkbox', checked: !t.container, click: () => this.setContainer(id, null) },
          ...(settings.containers || []).map((c) => ({
            label: c.name, type: 'checkbox', checked: t.container === c.name, click: () => this.setContainer(id, c.name)
          }))
        ]
      },
      { label: 'Bookmark this tab', enabled: http, click: () => this.sendChrome('cmd', 'bookmark') },
      { type: 'separator' },
      { label: 'Close tab', click: () => this.closeTab(id) },
      { label: 'Close other tabs', enabled: this.order.length > 1, click: () => this.closeOtherTabs(id) },
      {
        label: 'Close tabs to the right',
        enabled: this.order.indexOf(id) < this.order.length - 1,
        click: () => this.closeTabsToRight(id)
      }
    ]
  }

  closeTab(id, opts = {}) {
    const tab = this.tabs.get(id)
    if (!tab) return
    if (this.splitId === id) this.splitId = null
    lastClosedTabs.push(tab.url)
    if (lastClosedTabs.length > 20) lastClosedTabs.shift()

    if (this.activeId === id) {
      const idx = this.order.indexOf(id)
      const nextId = this.order[idx + 1] || this.order[idx - 1]
      this.activeId = null
      if (nextId) this.activate(nextId)
    }
    this.order = this.order.filter(x => x !== id)
    this.tabs.delete(id)
    wcTabs.delete(tab.wcId)
    try { this.win.contentView.removeChildView(tab.view) } catch { /* view may already be detached */ }
    try { tab.view.webContents.close() } catch { /* contents may already be destroyed */ }

    if (this.order.length === 0 && !opts.keepOpen) {
      if (mainWindows.size > 1) this.win.close()
      else this.createTab(settings.homepage)
    }
    this.sendState()
  }

  reopenClosed() {
    if (!lastClosedTabs.length) return
    const url = lastClosedTabs.pop()
    this.createTab(url)
  }

  toggleSplit() {
    if (this.splitId) {
      this.splitId = null
      this.layout()
      this.sendState()
      return
    }
    if (!this.activeId) return
    const idx = this.order.indexOf(this.activeId)
    let other = this.order[idx + 1] || this.order[idx - 1]
    if (!other || other === this.activeId) {
      const t = this.createTab('psychovault://newtab', { activate: false })
      other = t.id
    }
    this.splitId = other
    for (const [tid, t] of this.tabs) t.view.setVisible(tid === this.activeId || tid === this.splitId)
    this.layout()
    this.sendState()
  }

  layoutSoon() {
    if (this._layoutTimer) return
    this._layoutTimer = setTimeout(() => {
      this._layoutTimer = null
      this.layout()
      // second pass: catches the drag ending on a frame we should not have trusted
      this._settleTimer = setTimeout(() => {
        this._settleTimer = null
        this.layout()
      }, 140)
    }, 24)
  }

  layout() {
    if (!this.win || this.win.isDestroyed()) return
    if (this.win.isMinimized()) return
    const b = this.win.getContentBounds()
    const width = b && b.width, height = b && b.height
    // Degenerate size: keep the last good geometry and retry rather than blank the
    // page views. This is the "tabs vanish when I drag the window" bug.
    if (!width || !height || width < 200 || height < 200) {
      if ((this._layoutRetries || 0) < 10) {
        this._layoutRetries = (this._layoutRetries || 0) + 1
        clearTimeout(this._retryTimer)
        this._retryTimer = setTimeout(() => { this._layoutRetries = 0; this.layout() }, 160)
      }
      return
    }
    this._layoutRetries = 0
    const full = this.win.isFullScreen()
    const rail = settings.hideSidebar ? 0 : RAIL_W
    const top = full ? 0 : CHROME_H + this.extraTop
    const left = full ? 0 : rail + (this.panelOpen ? PANEL_W : 0)
    if (this.chromeModal) {
      // A full-window chrome overlay (command palette) is up: park the pages.
      for (const [, t] of this.tabs) t.view.setBounds({ x: 0, y: 0, width: 0, height: 0 })
      return
    }
    const W = Math.max(0, width - left)
    const H = Math.max(0, height - top - (full ? 0 : STATUS_H))
    const half = Math.floor(W / 2)
    for (const [tid, t] of this.tabs) {
      const main = tid === this.activeId
      const side = tid === this.splitId
      t.view.setVisible(main || side)
      if (!main && !side) continue
      if (main) {
        const w = side ? half - 1 : W
        t.view.setBounds({ x: left, y: top, width: w, height: H })
      } else {
        t.view.setBounds({ x: left + half + 1, y: top, width: W - half - 1, height: H })
      }
    }
  }

  recordHistory(url, title) {
    if (this.private) return
    if (settings.remember === false) return
    if (!/^https?:/.test(url)) return
    history.unshift({ url: storeUrl(url), title: title || storeUrl(url), at: Date.now() })
    // dedupe by url, keep newest 2000
    const seen = new Set()
    history = history.filter(h => {
      if (seen.has(h.url)) return false
      seen.add(h.url)
      return true
    }).slice(0, 2000)
    clearTimeout(this._hTimer)
    this._hTimer = setTimeout(() => writeJSON('history.json', history), 1500)
  }

  wsState() {
    return {
      urls: this.order.map((id) => storeUrl(this.tabs.get(id)?.url || '')).filter(Boolean),
      // (each of these reads uses ?. so an orphan id yields nothing rather than throwing)
      flags: this.order.map((id) => {
        const t = this.tabs.get(id)
        return t ? { pinned: !!t.pinned, muted: !!t.muted, container: t.container || null, service: !!t.service } : {}
      }),
      active: Math.max(0, this.order.indexOf(this.activeId))
    }
  }

  wsLoad(set) {
    for (const id of [...this.order]) this.closeTab(id, { keepOpen: true })
    const urls = set && set.urls && set.urls.length ? set.urls : [settings.homepage]
    for (const [i, u] of urls.entries()) {
      const f = (set.flags && set.flags[i]) || {}
      this.createTab(u, { activate: false, pinned: !!f.pinned, container: f.container || null, service: !!f.service })
      if (f.muted) this.setMuted(this.order[this.order.length - 1], true)
    }
    const at = Math.min((set && set.active) || 0, this.order.length - 1)
    this.activate(this.order[Math.max(0, at)])
  }

  wsSwitch(i) {
    if (this.private || !workspaces.list[i]) return
    if (i === workspaces.active) { this.sendState(); return }
    workspaces.list[workspaces.active] = { ...workspaces.list[workspaces.active], ...this.wsState() }
    workspaces.active = i
    saveWorkspaces()
    this.wsLoad(workspaces.list[i])
    wsBroadcast()
    this.sendState()
  }

  wsNew(name) {
    if (this.private) return
    workspaces.list[workspaces.active] = { ...workspaces.list[workspaces.active], ...this.wsState() }
    workspaces.list.push({ name: String(name || 'Workspace').trim().slice(0, 24) || 'Workspace', urls: [], flags: [], active: 0 })
    workspaces.active = workspaces.list.length - 1
    saveWorkspaces()
    this.wsLoad({ urls: [settings.homepage], flags: [{}], active: 0 })
    wsBroadcast()
    this.sendState()
  }

  // Closing a workspace. With more than one, it goes away; with the last one there is
  // nothing to remove without breaking the invariant that a window always has a home, so
  // the same control empties it back to the home page instead.
  wsClose(i) {
    if (this.private) return { ok: false, why: 'private windows have no workspaces' }
    if (workspaces.list.length < 2) {
      workspaces.list[0] = { ...workspaces.list[0], urls: [], flags: [], active: 0 }
      saveWorkspaces()
      if (workspaces.active === 0) this.wsLoad({ urls: [settings.homepage], flags: [{}], active: 0 })
      this.sendState()
      wsBroadcast()
      return { ok: true, cleared: true, name: workspaces.list[0].name }
    }
    const wasActive = i === workspaces.active
    const gone = workspaces.list[i] && workspaces.list[i].name
    workspaces.list.splice(i, 1)
    workspaces.active = Math.max(0, Math.min(workspaces.active, workspaces.list.length - 1))
    if (i < workspaces.active) workspaces.active -= 1
    saveWorkspaces()
    if (wasActive) this.wsLoad(workspaces.list[workspaces.active])
    // without this the strip kept the closed workspace's tabs on screen
    this.sendState()
    wsBroadcast()
    return { ok: true, removed: gone, count: workspaces.list.length }
  }

  // labels for the chip's right-click menu, kept apart from the popup so it can be tested
  wsMenuItems(i) {
    const last = workspaces.list.length < 2
    const name = (workspaces.list[i] && workspaces.list[i].name) || 'workspace'
    return [
      { label: 'Switch to this workspace', enabled: i !== workspaces.active, click: () => this.wsSwitch(i) },
      { label: 'New workspace', click: () => this.wsNew('Workspace ' + (workspaces.list.length + 1)) },
      { type: 'separator' },
      { label: last ? 'Close all tabs in this workspace' : 'Close workspace "' + name + '"', click: () => this.wsClose(i) }
    ]
  }

  wsRename(i, name) {
    if (!workspaces.list[i]) return
    workspaces.list[i].name = String(name || 'Workspace').trim().slice(0, 24) || 'Workspace'
    saveWorkspaces()
    wsBroadcast()
  }

  saveSession() {
    if (this.private) return
    const urls = this.order.map(id => storeUrl(this.tabs.get(id)?.url || '')).filter(u => u && !u.startsWith('psychovault://settings'))
    const flags = this.order.map(id => {
      const t = this.tabs.get(id)
      return t ? { pinned: !!t.pinned, muted: !!t.muted, container: t.container || null, service: !!t.service } : {}
    })
    const active = this.order.indexOf(this.activeId)
    writeJSON('session.json', { urls, active: active < 0 ? 0 : active, flags })
    workspaces.list[workspaces.active] = { ...workspaces.list[workspaces.active], urls, flags, active: active < 0 ? 0 : active }
    saveWorkspaces()
  }

  sendChrome(channel, payload) {
    if (this.win && !this.win.isDestroyed()) this.win.webContents.send(channel, payload)
  }

  sendState() {
    if (!this.win || this.win.isDestroyed()) return
    // a window being torn down can fire a state push after its tabs are gone, and an
    // uncaught error here would leave every open window with a frozen tab strip
    this.order = this.order.filter((id) => this.tabs.has(id))
    const tabs = this.order.map(id => {
      const t = this.tabs.get(id)
      if (!t) return null
      return {
        id: t.id, title: t.title, url: t.url, favicon: t.favicon,
        loading: t.loading, blocked: t.blocked,
        pinned: !!t.pinned, muted: !!t.muted, discarded: !!t.discarded,
        container: t.container || null, island: t.island || null, service: !!t.service
      }
    }).filter(Boolean)
    const a = this.tabs.get(this.activeId)
    this.sendChrome('state', {
      tabs,
      activeId: this.activeId,
      maximized: this.win.isMaximized(),
      private: this.private,
      canGoBack: a ? a.canGoBack : false,
      canGoForward: a ? a.canGoForward : false,
      loading: a ? a.loading : false,
      url: a ? a.url : '',
      title: a ? a.title : '',
      settings,
      splitId: this.splitId,
      blockedTotal: globalBlocked,
      workspaces: wsPayload(),
      containers: settings.containers || [],
      services: servicesPayload(),
      spell: { enabled: spell.enabled, languages: spell.languages, missing: spell.missing, words: (settings.spellUserWords || []).length },
      sounds: { on: !!settings.uiSounds, vol: Number(settings.soundVol) || 0.4 },
      enhance: { video: !!settings.enhanceVideo, images: !!settings.enhanceImages },
      netLimit: Number(settings.netLimit) || 0,
      panicEnabled: !!settings.panicEnabled,
      modCss: String(settings.modCss || '').slice(0, 4000),
      panicked: !!this._panicked,
      vault: vaultStatus(),
      battery: !!settings.batterySaver
    })
  }

  // Text fields inside pages: Chromium gives the menu no items of its own in
  // Electron, so paste has to be wired up here. Roles are not usable because the
  // focused field lives in the page view, not in this window's webContents.
  showPageMenu(params, tab) {
    builtinSpellAt(tab.view.webContents).then((builtin) => {
      let menu
      try { menu = Menu.buildFromTemplate(this.pageMenuTemplate(params, tab, builtin)) }
      catch (e) { logMainError(e); return }
      menu.popup({ window: this.win })
    })
  }

  pageMenuTemplate(params, tab, builtin) {
    const wc = tab.view.webContents
    if (params.isEditable) {
      const f = params.editFlags || {}
      const items = [
        { label: 'Cut', enabled: !!f.canCut, click: () => wc.cut() },
        { label: 'Copy', enabled: !!f.canCopy, click: () => wc.copy() },
        { label: 'Paste', enabled: f.canPaste !== false, click: () => wc.paste() },
        { label: 'Paste and match style', enabled: f.canPaste !== false, click: () => wc.pasteAndMatchStyle() },
        { label: 'Delete', enabled: !!f.canDelete, click: () => wc.delete() },
        { type: 'separator' },
        { label: 'Select all', enabled: !!f.canSelectAll, click: () => wc.selectAll() },
        { label: 'Undo', enabled: !!f.canUndo, click: () => wc.undo() },
        { label: 'Redo', enabled: !!f.canRedo, click: () => wc.redo() }
      ]
      // Electron replaces the whole menu, so the suggestion row Chromium would show has to
      // be built by hand. When its dictionary machinery has nothing (the usual case in an
      // Electron build), the page's own checker answers instead.
      const fromChromium = (Array.isArray(params.dictionarySuggestions) ? params.dictionarySuggestions : []).slice(0, 5)
      const word = params.misspelledWord || (builtin && builtin.word) || ''
      const sugg = fromChromium.length ? fromChromium : ((builtin && builtin.suggestions) || []).slice(0, 5)
      const engine = fromChromium.length ? 'chromium' : 'builtin'
      if (word) {
        items.push({ type: 'separator' })
        if (sugg.length) {
          for (const fix of sugg) {
            items.push({
              label: fix,
              click: () => {
                if (engine === 'chromium') { wc.replaceMisspelling(fix); return }
                if (!builtin) return
                const args = JSON.stringify([builtin.start, builtin.end, fix])
                wc.executeJavaScript('window.__pvSpellApply && window.__pvSpellApply.apply(null, ' + args + ')')
                  .catch(() => { /* the field lost focus between the click and now */ })
              }
            })
          }
          items.push({ label: engine === 'chromium' ? 'dictionary: browser' : 'suggestion by PsychoVault', enabled: false })
        } else {
          items.push({ label: 'No suggestions - no dictionary for this language yet', enabled: false })
        }
        items.push({
          label: `Add “${String(word).slice(0, 32)}” to dictionary`,
          click: () => { addSpellWord(wc.session, word); tellPages('__pvSpellAdd', word) }
        })
      } else if (f.isMissingSpellChecker) {
        items.push({ type: 'separator' })
        items.push({ label: 'Spelling: no dictionary loaded (Settings - Spelling)', enabled: false })
      }
      if (params.selectionText) {
        items.push({ type: 'separator' })
        items.push({ label: `Search for "${params.selectionText.slice(0, 24)}"`, click: () => this.searchSelection(params.selectionText) })
      }
      const vaultItems = this.vaultMenuItems(tab, params)
      if (vaultItems.length) { items.push({ type: 'separator' }); items.push(...vaultItems) }
      return items
    }
    const items = []
    if (params.linkURL) {
      items.push({ label: 'Open link in new tab', click: () => this.createTab(params.linkURL) })
      items.push({ label: 'Copy link address', click: () => clipboard.writeText(params.linkURL) })
      items.push({ type: 'separator' })
    }
    if (params.srcURL) {
      items.push({ label: 'Open image in new tab', click: () => this.createTab(params.srcURL) })
      items.push({ label: 'Copy image', click: () => { tab.view.webContents.copyImageAt(params.x, params.y) } })
      items.push({ type: 'separator' })
    }
    if (params.selectionText) {
      items.push({ label: `Search for "${params.selectionText.slice(0, 24)}"`, click: () => this.searchSelection(params.selectionText) })
      items.push({ label: 'Copy', click: () => clipboard.writeText(params.selectionText) })
      items.push({ type: 'separator' })
    }
    items.push({ label: 'Back', enabled: tab.canGoBack, click: () => tab.view.webContents.navigationHistory.goBack() })
    items.push({ label: 'Forward', enabled: tab.canGoForward, click: () => tab.view.webContents.navigationHistory.goForward() })
    items.push({ label: 'Reload', click: () => tab.view.webContents.reload() })
    items.push({ type: 'separator' })
    items.push({ label: 'Inspect element', click: () => tab.view.webContents.inspectElement(params.x, params.y) })
    return items
  }
}

let globalBlocked = 0
const wcTabs = new Map()        // webContents id -> tab, for per-page counters
const statWindows = new Set()   // live windows to refresh
let statTimer = null

function noteBlocked(webContentsId) {
  globalBlocked++
  const tab = wcTabs.get(webContentsId)
  if (tab) tab.blocked++
  // many requests are blocked on one page load; coalesce the repaint
  if (!statTimer) {
    statTimer = setTimeout(() => {
      statTimer = null
      for (const w of statWindows) {
        try { if (!w.win.isDestroyed()) w.sendState() } catch (e) { /* window went away */ }
      }
    }, 250)
  }
}

// ---------------------------------------------------------------- IPC
ipcMain.handle('tab:new', (e, url) => {
  const w = [...mainWindows].find(w => w.win.webContents === e.sender)
  if (w) w.createTab(url || 'psychovault://newtab')
})
ipcMain.handle('tab:open', (e, rawUrl) => {
  const tab = wcTabs.get(e.sender.id)
  const w = tab && [...mainWindows].find((owner) => owner.tabs.get(tab.id) === tab)
  if (!w) return { ok: false, error: 'browser tab not found' }
  let url
  try { url = new URL(String(rawUrl || '')) } catch { return { ok: false, error: 'invalid URL' } }
  if (url.protocol !== 'https:' && url.protocol !== 'http:') return { ok: false, error: 'only web links can be opened' }
  w.createTab(url.href, { parentId: tab.id, container: tab.container || null })
  return { ok: true }
})
ipcMain.handle('translate:page', async (e) => {
  const w = findWin(e)
  const tab = w && w.tabs.get(w.activeId)
  if (!w || !tab) return { ok: false, error: 'There is no active page to translate.' }
  const translation = buildTranslationUrl(tab.url, settings.translateTarget)
  if (!translation.ok) return translation

  let answer
  try {
    answer = await dialog.showMessageBox(w.win, {
      type: 'info',
      title: 'Translate page with Google Translate?',
      message: `Translate this page to ${translation.languageName}?`,
      detail: 'Google Translate will receive this page URL and retrieve its contents. PsychoVault opens the translated copy in a new tab, leaving the original untouched. Avoid this for private or sensitive pages.',
      buttons: ['Cancel', 'Continue'],
      defaultId: 0,
      cancelId: 0,
      noLink: true
    })
  } catch (error) {
    logMainError(error)
    return { ok: false, error: 'Could not open the translation confirmation.' }
  }
  if (answer.response !== 1) return { ok: false, cancelled: true }
  if (w.win.isDestroyed() || !w.tabs.has(tab.id)) return { ok: false, error: 'The source tab was closed.' }
  w.createTab(translation.url, { parentId: tab.id, container: tab.container || null })
  return { ok: true, language: translation.languageName }
})
ipcMain.handle('tab:close', (e, id) => {
  const w = [...mainWindows].find(w => w.win.webContents === e.sender)
  if (w) w.closeTab(id)
})
ipcMain.handle('tab:activate', (e, id) => {
  const w = [...mainWindows].find(w => w.win.webContents === e.sender)
  if (w) w.activate(id)
})
ipcMain.handle('tab:menu', (e, o) => {
  const w = findWin(e)
  if (w && o && Number.isFinite(o.id)) w.showTabMenu(o.id, o)
})
ipcMain.handle('tab:pin', (e, o) => {
  const w = findWin(e)
  if (w && o) w.setPinned(o.id, o.pinned)
})
ipcMain.handle('tab:mute', (e, o) => {
  const w = findWin(e)
  if (w && o) w.setMuted(o.id, o.muted)
})
ipcMain.handle('tab:move', (e, o) => {
  const w = findWin(e)
  if (!w || !o) return
  const id = Number(o.id)
  if (!Number.isFinite(id)) return
  const before = o.before == null ? null : Number(o.before)
  w.moveTab(id, Number.isFinite(before) ? before : null)
})
ipcMain.handle('tab:reopen', (e) => {
  const w = [...mainWindows].find(w => w.win.webContents === e.sender)
  if (w) w.reopenClosed()
})

ipcMain.handle('nav:go', (e, input) => {
  const w = [...mainWindows].find(w => w.win.webContents === e.sender)
  if (!w) return
  const t = w.tabs.get(w.activeId)
  if (t) w.go(t, input)
})
ipcMain.handle('nav:back', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.navigationHistory.goBack() })
ipcMain.handle('nav:forward', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.navigationHistory.goForward() })
ipcMain.handle('nav:reload', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.reload() })
ipcMain.handle('nav:stop', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.stop() })
ipcMain.handle('nav:home', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) w.go(t, settings.homepage) })

// GX's sidebar player is mostly about not touching the keyboard: the media keys
// drive whichever tab holds audio, and the music pauses itself when a video
// somewhere else starts making noise.
let mediaPausedByUs = null
function registerMediaKeys() {
  const map = { 'MediaPlayPause': 'toggle', 'MediaStop': 'pause', 'MediaNextTrack': 'next', 'MediaPreviousTrack': 'prev' }
  for (const [accel, cmd] of Object.entries(map)) {
    try {
      globalShortcut.register(accel, () => {
        if (!settings.mediaKeys) return
        const w = [...mainWindows][0]
        if (w) w.mediaRoute(cmd)
      })
    } catch { /* another app holds the key, or no display */ }
  }
}

function watchPower() {
  if (!powerMonitor) return
  const apply = (onBattery) => {
    if (!settings.batteryAuto) return
    if (onBattery === settings.batterySaver) return
    settings = { ...settings, batterySaver: onBattery }
    writeJSON('settings.json', settings)
    applyCpuLimit()
    for (const w of mainWindows) w.sendState()
  }
  try {
    powerMonitor.on('on-battery', () => apply(true))
    powerMonitor.on('on-ac-power', () => apply(false))
  } catch { /* headless */ }
}

// "Automatically pause playback when other media start playing", GX's player switch.
setInterval(() => {
  if (!settings.mediaAutoPause) return
  for (const w of mainWindows) {
    const tabs = [...w.tabs.values()]
    const svc = tabs.find((t) => t.service && !t.discarded)
    if (!svc) continue
    let other = false
    for (const t of tabs) {
      if (t === svc || t.discarded) continue
      if (safe(() => t.view.webContents.isCurrentlyAudible(), false)) { other = true; break }
    }
    const svcAudible = safe(() => svc.view.webContents.isCurrentlyAudible(), false)
    if (other && svcAudible) { w.mediaRoute('pause'); mediaPausedByUs = svc.id }
    else if (!other && mediaPausedByUs === svc.id) { w.mediaRoute('play'); mediaPausedByUs = null }
  }
}, 2500)

function findWin(e) {
  return [...mainWindows].find(w => w.win.webContents === e.sender)
}

ipcMain.handle('ui:layout', (e, o) => {
  const w = findWin(e)
  if (!w) return
  if (typeof o.extraTop === 'number') w.extraTop = Math.max(0, Math.min(600, o.extraTop))
  if (typeof o.panel === 'boolean') w.panelOpen = o.panel
  if (typeof o.palette === 'boolean') {
    w.chromeModal = o.palette
    if (o.palette) {
      w.win.webContents.focus()
    } else {
      const t = w.tabs.get(w.activeId)
      if (t) t.view.webContents.focus()
    }
  }
  w.layout()
})

ipcMain.handle('view:split', (e) => { const w = findWin(e); if (w) w.toggleSplit() })

ipcMain.handle('win:minimize', (e) => { const w = findWin(e); if (w) w.win.minimize() })
ipcMain.handle('win:maximize', (e) => {
  const w = findWin(e)
  if (!w) return
  if (w.win.isMaximized()) w.win.unmaximize(); else w.win.maximize()
})
ipcMain.handle('win:close', (e) => { const w = findWin(e); if (w) w.win.close() })
ipcMain.handle('win:private', () => {
  const w = new TabWindow({ private: true })
  mainWindows.add(w)
  return true
})

ipcMain.handle('bookmarks:list', () => bookmarks)
ipcMain.handle('bookmarks:add', (e, bm) => {
  if (!bookmarks.find(b => b.url === bm.url)) {
    bookmarks.unshift({ ...bm, at: Date.now() })
    writeJSON('bookmarks.json', bookmarks)
  }
  return bookmarks
})
ipcMain.handle('bookmarks:remove', (e, url) => {
  bookmarks = bookmarks.filter(b => b.url !== url)
  writeJSON('bookmarks.json', bookmarks)
  return bookmarks
})
ipcMain.handle('bookmarks:isBookmarked', (e, url) => !!bookmarks.find(b => b.url === url))

ipcMain.handle('history:list', () => history.slice(0, 300))
ipcMain.handle('history:clear', () => { history = []; writeJSON('history.json', []); return [] })
ipcMain.handle('history:search', (e, q) => {
  const s = (q || '').toLowerCase()
  if (!s) return []
  const out = []
  for (const b of bookmarks) {
    if (b.url.toLowerCase().includes(s) || (b.title || '').toLowerCase().includes(s)) out.push({ ...b, type: 'bookmark' })
  }
  for (const h of history) {
    if (h.url.toLowerCase().includes(s) || (h.title || '').toLowerCase().includes(s)) out.push({ ...h, type: 'history' })
    if (out.length > 8) break
  }
  return out.slice(0, 8)
})

// What is actually on disk, and a way to erase any of it. Used by the settings
// page's "Your data" panel.
ipcMain.handle('data:report', async () => {
  const files = []
  try {
    for (const name of fs.readdirSync(USER)) {
      const full = path.join(USER, name)
      let st
      try { st = fs.statSync(full) } catch { continue }
      if (st.isDirectory()) continue
      files.push({ name, bytes: st.size })
    }
    files.sort((a, b) => b.bytes - a.bytes)
  } catch (e) { /* unreadable profile dir */ }
  let cookies = 0
  try { cookies = (await session.fromPartition('persist:psychovault').cookies.get({})).length } catch (e) { /* no session yet */ }
  return { dir: USER, files: files.slice(0, 40), cookies }
})

ipcMain.handle('data:clear', (e, what) => {
  const ses = session.fromPartition('persist:psychovault')
  const done = []
  const run = (label, fn) => {
    try { fn(); done.push(label) } catch (err) { console.error('clear failed:', label, err.message) }
  }
  const w = what || {}
  if (w.cache) run('cache', () => ses.clearCache())
  if (w.cookies) run('cookies', () => ses.clearStorageData({ storages: ['cookies'] }))
  if (w.storage) run('site data', () => ses.clearStorageData({
    storages: ['localstorage', 'indexdb', 'websql', 'serviceworkers', 'cachestorage']
  }))
  if (w.history) run('history', () => { history = []; writeJSON('history.json', []) })
  if (w.downloads) run('downloads', () => { downloads = []; saveDownloads(); broadcast('downloads', downloads) })
  if (w.session) run('saved session', () => writeJSON('session.json', { urls: [], active: 0 }))
  return { done }
})

// ---- GX extras
ipcMain.handle('gx:hot', (e, o) => {
  const w = findWin(e)
  if (!w) return []
  if (o && o.action) {
    if (o.action === 'sleep') w.discardTab(Number(o.id))
    else if (o.action === 'wake') w.revive(Number(o.id))
    else if (o.action === 'close') w.closeTab(Number(o.id))
    w.sendState()
    return w.hotTabs()
  }
  return w.hotTabs()
})
ipcMain.handle('gx:media', (e, cmd) => { const w = findWin(e); return w ? w.mediaRoute(cmd) : { ok: false } })
ipcMain.handle('gx:snapshot', (e, kind) => { const w = findWin(e); return w ? w.capturePage(kind) : { ok: false } })
ipcMain.handle('gx:panic', (e, on) => { const w = findWin(e); return w ? w.panic(!!on) : { ok: false } })
ipcMain.handle('gx:services', (e, o) => {
  const w = findWin(e)
  if (!w) return { ok: false }
  const svc = (settings.services || [])[Number(o.i)]
  if (!svc) return { ok: false, reason: 'no such service' }
  if (o.action === 'reload') {
    const t = [...w.tabs.values()].find((x) => x.service && x.url.includes(svc.url))
    if (t) t.view.webContents.reload()
    return { ok: true }
  }
  if (o.action === 'remove') return removeService(Number(o.i))
  if (o.action === 'copy') { try { clipboard.writeText(svc.url || '') } catch (e) { logMainError(e) }; return { ok: true } }
  if (o.action === 'window') {
    const nw = new TabWindow({ private: w.private })
    mainWindows.add(nw)
    nw.createTab(svc.url, { activate: true })
    return { ok: true, window: true }
  }
  if (o.action === 'menu') {
    const tpl = [
      { label: 'Open ' + (svc.name || 'service'), click: () => { const t = [...w.tabs.values()].find((x) => x.service && (x.url || '').startsWith(svc.url)); if (t) w.activate(t.id); else w.createTab(svc.url, { service: true, activate: true }) } },
      { label: 'Open in a new window', click: () => { const nw = new TabWindow({ private: w.private }); mainWindows.add(nw); nw.createTab(svc.url, { activate: true }) } },
      { label: 'Reload', click: () => { const t = [...w.tabs.values()].find((x) => x.service && x.url.includes(svc.url)); if (t) t.view.webContents.reload() } },
      { type: 'separator' },
      { label: 'Copy address', click: () => { try { clipboard.writeText(svc.url || '') } catch (e) { logMainError(e) } } },
      { label: 'Remove from rail', click: () => removeService(Number(o.i)) }
    ]
    const menu = Menu.buildFromTemplate(tpl)
    const pt = screen.getCursorScreenPoint()
    menu.popup({ window: w.win, x: pt.x, y: pt.y })
    return { ok: true, items: tpl.filter((x) => x.label).map((x) => x.label) }
  }
  const existing = [...w.tabs.values()].find((x) => x.service && (x.url || '').startsWith(svc.url))
  if (existing) { w.activate(existing.id); return { ok: true, reused: true } }
  w.createTab(svc.url, { service: true, activate: true })
  return { ok: true, opened: svc.name }
})
function removeService(i) {
  const list = [...(Array.isArray(settings.services) ? settings.services : [])]
  if (i < 0 || i >= list.length) return { ok: false, why: 'nothing at that index' }
  list.splice(i, 1)
  settings = { ...settings, services: list }
  writeJSON('settings.json', settings)
  for (const w of mainWindows) w.sendState()
  return { ok: true, count: list.length }
}
ipcMain.handle('app:relaunch', () => { app.relaunch(); app.exit(0); return { ok: true } })

// ---------------------------------------------------------------- rail / spell IPC
ipcMain.handle('rail:icons', () => railIcons())
ipcMain.handle('catalogue:list', () => SERVICE_CATALOGUE.map((c) => ({ ...c, host: hostOf(c.url), on: (settings.services || []).some((sv) => hostOf(sv.url) === hostOf(c.url)) })))
ipcMain.handle('catalogue:toggle', (e, name) => {
  const cat = SERVICE_CATALOGUE.find((c) => c.name === name)
  if (!cat) return { ok: false, why: 'unknown service' }
  let list = Array.isArray(settings.services) ? [...settings.services] : []
  const at = list.findIndex((sv) => hostOf(sv.url) === hostOf(cat.url))
  if (at >= 0) list.splice(at, 1)
  else {
    if (list.length >= RAIL_MAX) return { ok: false, why: 'the rail holds ' + RAIL_MAX + ' - remove one first' }
    list.push({ name: cat.name, url: cat.url })
  }
  settings = { ...settings, services: list }
  writeJSON('settings.json', settings)
  updateServiceIcons()
  for (const w of mainWindows) w.sendState()
  return { ok: true, on: at < 0, count: list.length }
})

const sesOfSender = (e) => {
  const w = findWin(e)
  const t = w && w.tabs.get(w.activeId)
  return (t && t.view.webContents.session) || session.defaultSession
}
ipcMain.handle('spell:status', (e) => ({
  ...spell,
  builtin: { on: spellBuiltinOn(), words: SPELL_WORDS.length, page: !!SPELL_PAGE, engine: spellBuiltinOn() ? 'PsychoVault' : 'none' },
  words: (settings.spellUserWords || []).slice(-40),
  locale: app.getLocale(),
  want: spellWant(),
  available: (session.defaultSession.availableSpellCheckerLanguages || []).slice(0, 90)
}))
ipcMain.handle('spell:add', (e, word) => {
  const r = addSpellWord(sesOfSender(e), word)
  tellPages('__pvSpellAdd', String(word || '').toLowerCase())
  return r
})
ipcMain.handle('spell:remove', (e, word) => {
  const r = dropSpellWord(sesOfSender(e), word)
  // the page keeps its own copy of your words, so give it the new list rather than
  // trying to unlearn one
  applySpellToAllPages()
  return r
})

ipcMain.handle('gx:switches', () => {
  const cl = app.commandLine
  const g = (name) => (cl.hasSwitch && cl.hasSwitch(name) ? cl.getSwitchValue(name) : null)
  return {
    webrtc: g('force-webrtc-ip-handling-policy'),
    dohMode: g('dns-over-https-mode'),
    dohTemplate: g('dns-over-https-template'),
    quic: cl.hasSwitch && cl.hasSwitch('disable-quic'),
    restartNeeded: switchDrift()
  }
})
// The switches are read once at startup, so say so when the file disagrees.
function switchDrift() {
  const cl = app.commandLine
  const policy = { default: '', publiconly: 'default_public_interface_only', disableudp: 'disable_non_proximity_udp' }
  const live = cl.hasSwitch('force-webrtc-ip-handling-policy') ? cl.getSwitchValue('force-webrtc-ip-handling-policy') : ''
  return live !== (policy[settings.webRtc] || '') ||
    (cl.hasSwitch('dns-over-https-mode') ? cl.getSwitchValue('dns-over-https-mode') : '') !==
    (settings.doh === 'off' ? '' : settings.doh)
}

// ---- workspaces
ipcMain.handle('ws:list', () => wsPayload())
ipcMain.handle('ws:switch', (e, i) => { const w = findWin(e); if (w) w.wsSwitch(Number(i)) })
ipcMain.handle('ws:new', (e, name) => { const w = findWin(e); if (w) w.wsNew(name) })
ipcMain.handle('ws:close', (e, i) => { const w = findWin(e); if (w) w.wsClose(Number(i)) })
ipcMain.handle('ws:rename', (e, o) => { const w = findWin(e); if (w) w.wsRename(Number(o.i), o.name) })
ipcMain.handle('ws:menu', (e, i) => {
  const w = findWin(e)
  if (!w || w.private) return { ok: false }
  const tpl = w.wsMenuItems(Number(i))
  const menu = Menu.buildFromTemplate(tpl)
  const pt = screen.getCursorScreenPoint()
  // popped at the pointer, which is where the chip that asked for it is
  menu.popup({ window: w.win, x: pt.x, y: pt.y })
  return { ok: true, items: tpl.filter((x) => x.label).map((x) => x.label) }
})
ipcMain.handle('clipboard:write', (e, text) => {
  // the chrome page is not focused like a web page, and navigator.clipboard depends on
  // focus; writing through the main process is the reliable way for a palette command
  try { clipboard.writeText(String(text == null ? '' : text)); return { ok: true } }
  catch (err) { logMainError(err); return { ok: false } }
})

// ---- password vault
ipcMain.handle('vault:status', () => vaultStatus())
ipcMain.handle('vault:list', () => {
  if (settings.vaultEnabled === false) return { error: 'disabled', entries: [], status: vaultStatus() }
  if (vaultLocked && !vaultUnlock().ok) return { error: 'locked', entries: [], status: vaultStatus() }
  return {
    entries: (vaultCache || []).map((e) => ({ id: e.id, site: e.site, user: e.user, note: e.note || '', at: e.at })),
    status: vaultStatus()
  }
})
ipcMain.handle('vault:unlock', (e, pw) => {
  const r = vaultUnlock(pw)
  if (r.ok) vaultBroadcast()
  return { ...r, status: vaultStatus() }
})
ipcMain.handle('vault:lock', () => {
  vaultLocked = true
  vaultCache = null
  vaultKey = null
  clearTimeout(vaultTimer)
  vaultBroadcast()
  return vaultStatus()
})
ipcMain.handle('vault:add', (e, entry) => {
  if (vaultLocked && !vaultUnlock().ok) return { ok: false, reason: 'the vault is locked', status: vaultStatus() }
  const clean = {
    id: entry.id || crypto.randomUUID(),
    site: String(entry.site || '').trim().replace(/^https?:\/\//, '').replace(/\/.*$/, ''),
    user: String(entry.user || '').slice(0, 200),
    pass: String(entry.pass || '').slice(0, 512),
    note: String(entry.note || '').slice(0, 2000),
    at: Date.now()
  }
  if (!clean.site || !clean.pass) return { ok: false, reason: 'site and password are both required', status: vaultStatus() }
  const i = vaultCache.findIndex((x) => x.site === clean.site && x.user === clean.user)
  if (i >= 0) vaultCache[i] = { ...vaultCache[i], ...clean }
  else vaultCache.push(clean)
  const w = vaultWrite()
  if (!w.ok) vaultCache.splice(i >= 0 ? i : vaultCache.length - 1, 1)
  vaultArmLock()
  vaultBroadcast()
  return { ...w, status: vaultStatus() }
})
ipcMain.handle('vault:delete', (e, id) => {
  if (vaultLocked) return { ok: false, reason: 'the vault is locked' }
  vaultCache = (vaultCache || []).filter((x) => x.id !== id)
  const w = vaultWrite()
  vaultBroadcast()
  return { ...w, status: vaultStatus() }
})
ipcMain.handle('vault:reveal', (e, id) => {
  if (vaultLocked) return { error: 'locked' }
  const m = (vaultCache || []).find((x) => x.id === id)
  vaultArmLock()
  return m ? { pass: m.pass } : { error: 'not found' }
})
// Copying a password re-arms a short auto-lock: the clipboard is the leakiest place.
ipcMain.handle('vault:copy', (e, o) => {
  if (vaultLocked) return { error: 'locked' }
  const m = (vaultCache || []).find((x) => x.id === o.id)
  if (!m) return { error: 'not found' }
  clipboard.writeText(o.which === 'user' ? m.user : m.pass)
  vaultArmLock()
  if (o.which !== 'user') {
    clearTimeout(clipboardLockTimer)
    clipboardLockTimer = setTimeout(() => {
      clipboard.writeText('')
      vaultLocked = true; vaultCache = null; vaultKey = null; vaultBroadcast()
    }, 60000)
  }
  return { ok: true }
})
ipcMain.handle('vault:generate', (e, opts) => vaultGenerate(opts))
ipcMain.handle('vault:setMaster', (e, pw) => {
  const str = String(pw || '')
  if (str.length < 8) return { ok: false, reason: 'use at least 8 characters' }
  const salt = crypto.randomBytes(16)
  const key = vaultDerive(str, salt.toString('base64'))
  const check = vaultSeal(JSON.stringify({ v: 1 }), key).toString('base64')
  const carry = vaultCache
  try {
    writeJSON(VAULT_KEY, { salt: salt.toString('base64'), check, kdf: 'scrypt-N16384-r8-p1', aead: 'aes-256-gcm' })
    vaultKey = key
    vaultCache = carry || vaultLoad() || []
    const w = vaultWrite()
    if (!w.ok) return { ok: false, reason: w.reason }
    vaultLocked = false
    vaultArmLock()
    vaultBroadcast()
    return { ok: true, status: vaultStatus() }
  } catch (err) { return { ok: false, reason: err.message } }
})
ipcMain.handle('vault:forgetMaster', () => {
  const data = vaultCache || vaultLoad() || []
  try { fs.unlinkSync(path.join(USER, VAULT_KEY)) } catch (e) { /* nothing set */ }
  vaultKey = null
  vaultCache = data
  vaultLocked = false
  const w = vaultWrite()
  vaultBroadcast()
  return { ...w, status: vaultStatus() }
})

ipcMain.handle('settings:get', () => settings)
ipcMain.handle('settings:set', (e, patch) => {
  settings = { ...settings, ...patch }
  delete settings['askSaveLocation']
  settings.settingsVersion = 3
  writeJSON('settings.json', settings)
  loadBlocklist()
  applyCpuLimit()
  for (const ses of liveSessions) applySessionPolicy(ses)
  if ('enhanceVideo' in patch || 'enhanceImages' in patch || 'reduceMotion' in patch) {
    for (const w of mainWindows) for (const t of w.tabs.values()) w.applyEnhancements(t)
  }
  if ('vaultLockMin' in patch || 'vaultEnabled' in patch) vaultArmLock()
  if ('spellCheck' in patch || 'spellLangs' in patch || 'spellDictUrl' in patch || 'spellBuiltin' in patch || 'spellUserWords' in patch) { applySpellPolicyAll(); applySpellToAllPages() }
  if ('services' in patch || 'railIcons' in patch) { updateServiceIcons(); for (const w of mainWindows) w.sendState() }
  for (const w of mainWindows) { w.layout(); w.sendState() }
  wsBroadcast()
  return settings
})

ipcMain.handle('find:start', (e, text) => {
  const w = findWin(e); const t = w && w.tabs.get(w.activeId)
  if (t) return t.view.webContents.findInPage(text)
})
ipcMain.handle('find:stop', (e) => {
  const w = findWin(e); const t = w && w.tabs.get(w.activeId)
  if (t) t.view.webContents.stopFindInPage('clearSelection')
})
ipcMain.handle('zoom:in', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.setZoomLevel(t.view.webContents.getZoomLevel() + 0.5) })
ipcMain.handle('zoom:out', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.setZoomLevel(t.view.webContents.getZoomLevel() - 0.5) })
ipcMain.handle('zoom:reset', (e) => { const w = findWin(e); const t = w && w.tabs.get(w.activeId); if (t) t.view.webContents.setZoomLevel(0) })

ipcMain.handle('open:external', async (_e, rawUrl) => {
  let url
  try { url = new URL(String(rawUrl || '')) } catch { return { ok: false, error: 'invalid URL' } }
  if (url.protocol !== 'https:' && url.protocol !== 'http:') return { ok: false, error: 'only web links can be opened externally' }
  await shell.openExternal(url.href)
  return { ok: true }
})

ipcMain.handle('downloads:list', () => downloads)
ipcMain.handle('downloads:clear', () => { downloads = []; saveDownloads(); broadcast('downloads', downloads); return [] })
ipcMain.handle('downloads:open', (e, filePath) => {
  if (typeof filePath === 'string' && filePath) shell.openPath(filePath)
})
ipcMain.handle('downloads:reveal', (e, filePath) => {
  if (typeof filePath === 'string' && filePath) shell.showItemInFolder(filePath)
})
ipcMain.handle('downloads:dir', () => downloadDir())
ipcMain.handle('downloads:pickdir', (e) => {
  const w = findWin(e)
  const picked = dialog.showOpenDialogSync(w && w.win && !w.win.isDestroyed() ? w.win : null, {
    title: 'Where should downloads go?', properties: ['openDirectory', 'createDirectory'], buttonLabel: 'Use this folder'
  })
  if (!picked || !picked[0]) return downloadDir()
  settings = { ...settings, downloadDir: picked[0] }
  writeJSON('settings.json', settings)
  for (const win of mainWindows) win.sendState()
  return picked[0]
})

// Where files land: the folder you chose, else the system one. Read at download time so
// changing the setting takes effect on the next file without a restart.
function downloadDir() {
  const custom = String(settings.downloadDir || '').trim()
  return custom || app.getPath('downloads')
}

// A download is a thing the browser has to announce - where it is going, and when it
// arrived. In-window toast every time; a system notification when no window has focus,
// because that is precisely when a toast cannot be seen.
function notifyDownload(info) {
  broadcast('downloader', info)
  const focused = [...mainWindows].some((w) => w.win && !w.win.isDestroyed() && w.win.isFocused())
  if (focused || !settings.downloadNotify) return
  try {
    if (!Notification.isSupported()) return
    const verb = info.kind === 'done' ? 'Download finished' : info.kind === 'failed' ? 'Download failed' : 'Download started'
    const where = info.kind === 'start' ? 'Saving to ' + (info.path || '') : (info.path || '')
    new Notification({ title: verb, body: info.name + (where ? '\n' + where : '') }).show()
  } catch (e) { logMainError(e) }
}

// downloads
let downloads = readJSON('downloads.json', [])
function saveDownloads() {
  writeJSON('downloads.json', downloads.slice(0, 50))
}

// GX Control also wants a live number; the panel asks for it on demand.
let downloadRate = 0
setInterval(() => {
  let sum = 0
  for (const d of downloads) {
    const cur = (d.received || 0) - (d._last || 0)
    d._last = d.received || 0
    if (cur > 0) sum += cur
  }
  downloadRate = Math.round(sum / 2)
}, 2000)

function downloadDialogParent(webContents) {
  const alive = [...mainWindows].filter((w) => w.win && !w.win.isDestroyed())
  const tab = webContents && wcTabs.get(webContents.id)
  if (tab) {
    const owner = alive.find((w) => w.tabs.get(tab.id) === tab)
    if (owner) return owner.win
  }
  const focused = alive.find((w) => w.win.isFocused())
  return (focused || alive[0] || {}).win || null
}

function watchDownloads(ses) {
  if (ses._pvDl) return
  ses._pvDl = true
  ses.on('will-download', (_event, item, webContents) => {
    // Pause on the first line of will-download; do not spend time resolving paths
    // while Chromium can still write to its default destination.
    try { item.pause() } catch (e) {
      logMainError(e)
      try { item.cancel() } catch (cancelError) { logMainError(cancelError) }
      notifyDownload({ kind: 'failed', name: item.getFilename() || 'download', path: '' })
      return
    }
    const dir = downloadDir()
    try { fs.mkdirSync(dir, { recursive: true }) } catch (e) { logMainError(e) }
    const suggested = String(item.getFilename() || 'download').replace(/[\\/:*?"<>|]/g, '_').slice(0, 180) || 'download'
    const defaultPath = path.join(dir, suggested)
    const parent = downloadDialogParent(webContents)

    // The asynchronous native picker now runs with the item paused. Do not call
    // event.preventDefault(): that permanently cancels the transfer. Cancellation in the
    // picker calls item.cancel(); only a chosen path reaches tracking and item.resume().
    requestDownloadSavePath(item, parent, defaultPath, true).then((target) => {
      if (!target) return
      trackDownload(item, target, suggested)
      // Attach progress and completion listeners before the first resumed bytes can land.
      item.resume()
    }).catch((e) => {
      logMainError(e)
      try { item.cancel() } catch (cancelError) { logMainError(cancelError) }
      notifyDownload({ kind: 'failed', name: suggested, path: '' })
    })
  })
}

function trackDownload(item, target, suggested) {
  const entry = { name: item.getFilename() || suggested, received: 0, total: item.getTotalBytes(), state: 'progress', path: target, at: Date.now() }
  downloads.unshift(entry)
  if (downloads.length > 50) downloads.length = 50
  saveDownloads()
  broadcast('downloads', downloads)
  notifyDownload({ kind: 'start', name: entry.name, path: target })

  // Progress while the file moves, not only when it lands. Throttled to five pushes a
  // second - 'updated' fires per chunk and the strip does not need 400 repaints a second.
  let lastBeat = 0
  item.on('updated', (_ev, dlState) => {
    entry.received = item.getReceivedBytes()
    const total = item.getTotalBytes()
    if (total > 0) entry.total = total
    if (dlState === 'interrupted') entry.state = 'interrupted'
    const now = Date.now()
    if (now - lastBeat >= 200) { lastBeat = now; broadcast('downloads', downloads) }
  })
  // Bandwidth cap - GX's network limiter. Electron has no bytes-per-second hook,
  // so this is a duty cycle with cumulative accounting: the transfer is only
  // allowed to be "ahead of schedule", where schedule = bytes received / cap.
  // The cap is read when the download starts; changing it does not affect a file
  // already coming down, which is what a limiter should do anyway.
  const capKB = Number(settings.netLimit) || 0
  const capBytes = capKB * 1024
  let started = Date.now(), stalled = 0, stallFrom = 0, stallTimer = null
  if (capBytes > 0) {
    item.on('updated', () => {
      const state = item.getState()
      if (state !== 'progressing' || stallTimer) return
      const total = item.getReceivedBytes()
      if (total <= 0) return
      const busy = (Date.now() - started) - stalled
      const allowed = (total / capBytes) * 1000
      const ahead = allowed - busy
      if (ahead < -40) return                       // already behind schedule
      const wait = Math.max(40, Math.min(15000, ahead))
      stallFrom = Date.now()
      try { item.pause() } catch { return }
      stallTimer = setTimeout(() => {
        stallTimer = null
        stalled += Date.now() - stallFrom
        try { if (item.getState() === 'progressing') item.resume() } catch { /* finished */ }
      }, wait)
    })
  }
  item.once('done', (_e, state) => {
    if (stallTimer) { clearTimeout(stallTimer); stallTimer = null }
    entry.received = item.getReceivedBytes()
    const total = item.getTotalBytes()
    if (total > 0) entry.total = total
    entry.state = state
    entry.path = item.getSavePath() || entry.path
    saveDownloads()
    broadcast('downloads', downloads)
    if (state === 'completed') notifyDownload({ kind: 'done', name: entry.name, path: entry.path, size: entry.received })
    else if (state === 'interrupted') notifyDownload({ kind: 'failed', name: entry.name, path: entry.path })
  })
  return entry
}
function broadcast(ch, payload) {
  for (const w of mainWindows) w.sendChrome(ch, payload)
}

// ---------------------------------------------------------------- app lifecycle
// A URL handed to us on the command line (the installer registers PsychoVault
// as an http/https handler, so Windows calls us with the address as argv).
function urlFromArgv(argv) {
  for (const raw of argv.slice(1)) {
    if (/^https?:\/\//i.test(raw)) return raw
    if (/^\/\/./.test(raw)) return 'https:' + raw
  }
  return null
}

const gotLock = app.requestSingleInstanceLock()
if (!gotLock) {
  app.quit()
} else {
  app.on('second-instance', (_e, argv) => {
    const url = urlFromArgv(argv)
    const first = [...mainWindows][0]
    if (!first) return
    if (first.win.isMinimized()) first.win.restore()
    first.win.focus()
    if (url) first.createTab(url)
  })
}

































app.whenReady().then(() => {
  refreshSafeStorage()
  registerMediaKeys()
  watchPower()
  Menu.setApplicationMenu(null)
  applyCpuLimit()
  setInterval(discardIdleTabs, 30000)
  watchDownloads(session.fromPartition('persist:psychovault'))
  watchDownloads(session.defaultSession)
  trackSession(session.fromPartition('persist:psychovault'))
  trackSession(session.defaultSession)
  updateServiceIcons()
  extSync().catch((e) => logMainError(e))

  const w = new TabWindow()
  mainWindows.add(w)

  const cliURL = urlFromArgv(process.argv)
  if (cliURL) w.win.webContents.once('did-finish-load', () => w.createTab(cliURL))

  checkForUpdatesOnStartup({
    onProgress: ({ message }) => { if (message) broadcast('update-progress', { message }) },
    onError: (error) => logMainError(error)
  })

  app.on('activate', () => {
    if (mainWindows.size === 0) {
      const n = new TabWindow(); mainWindows.add(n)
    }
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})

app.on('before-quit', () => {
  for (const w of mainWindows) w.saveSession()
  writeJSON('bookmarks.json', bookmarks)
  writeJSON('history.json', history)

  // "Delete browsing data on close" - GX's Cleaner switch
  const mode = settings.clearOnExit
  if (mode && mode !== 'nothing') {
    const ses = session.fromPartition('persist:psychovault')
    try { ses.clearCache() } catch (e) { /* ignore */ }
    if (mode === 'all') {
      try { ses.clearStorageData() } catch (e) { /* ignore */ }
      writeJSON('session.json', { urls: [], active: 0 })
    }
  }
})
