'use strict'
// Preload half of the extension shim. Registered on every PsychoVault session as BOTH a frame
// preload and a service-worker preload, so it runs inside extension pages (popup.html,
// onboarding) and inside Manifest V3 background service workers.
//
// Why this file exists
// --------------------
// Electron implements a subset of the chrome.* APIs, and the pieces a browser VPN extension
// lives on are not in it: chrome.proxy (no implementation at all), chrome.privacy (same) and
// chrome.tabs.create (missing). Chromium's own stubs for proxy/privacy exist but answer with
//   Unknown Extension API - types.ChromeSetting.set
// so an extension that uses them believes it has a tunnel it does not have.
//
// This preload carries a bridge into an extension's own world, and app/ext-main-world.js -
// a plain script with no imports - replaces those three APIs on top of it. Every call reaches
// the main process, and from there the real network stack: chrome.proxy.settings.set becomes
// session.setProxy, chrome.privacy becomes setWebRTCIPHandlingPolicy, chrome.tabs reaches the
// real tab list. Nothing is faked, and every failure is reported rather than swallowed.
//
// Getting the installer INTO that world is the part that went wrong repeatedly: it is done
// three different ways here, and each one records what it did in the shim log so the next
// field report carries evidence instead of a guess.
const { contextBridge, ipcRenderer, webFrame } = require('electron')
const fs = require('fs')
const path = require('path')

const BRIDGE_NAME = '__pvExtensionBridge'
const INSTALLER = path.join(__dirname, 'ext-main-world.js')
const isServiceWorker = process.type === 'service-worker'

function currentURL() {
  try {
    if (isServiceWorker && self.location) return String(self.location.href || '')
    if (typeof location !== 'undefined' && location) return String(location.href || '')
  } catch (e) { /* context gone */ }
  return ''
}

const url = currentURL()
if (url.startsWith('chrome-extension://')) {
  const id = url.slice('chrome-extension://'.length).split('/')[0]

  // Every line the shim writes goes to a file in the profile, so a report like the one that
  // came with this round ("Unknown Extension API" from a page and a worker) can be answered
  // with what actually happened instead of another rebuild.
  const note = (line) => {
    try { ipcRenderer.invoke('extshim:log', { id, line }) } catch (e) { /* nothing to log to */ }
  }

  // Isolated-world bridge. Every call is tagged with the extension id; the main process checks
  // that tag against the sender so one extension cannot move another's proxy.
  const bridge = {
    proxyGet: () => ipcRenderer.invoke('extshim:proxy:get', { id }),
    proxySet: (value, scope) => ipcRenderer.invoke('extshim:proxy:set', { id, value, scope: scope || 'regular' }),
    proxyClear: (scope) => ipcRenderer.invoke('extshim:proxy:clear', { id, scope: scope || 'regular' }),
    webrtcGet: () => ipcRenderer.invoke('extshim:webrtc:get', { id }),
    webrtcSet: (value) => ipcRenderer.invoke('extshim:webrtc:set', { id, value }),
    // chrome.tabs, backed by the real tab list in main.
    tabsQuery: (query) => ipcRenderer.invoke('extshim:tabs:query', { id, query: query || {} }),
    tabsCreate: (details) => ipcRenderer.invoke('extshim:tabs:create', { id, details: details || {} }),
    tabsUpdate: (tabId, details) => ipcRenderer.invoke('extshim:tabs:update', { id, tabId, details: details || {} }),
    tabsRemove: (tabId) => ipcRenderer.invoke('extshim:tabs:remove', { id, tabId }),
    onTabsRemoved: (fn) => {
      ipcRenderer.on('extshim:tabs:removed', (_e, tabId) => { try { fn(tabId) } catch (e) { /* extension threw */ } })
    },
    // Tells the main process whether the APIs actually went in. Without this a failed
    // injection looks identical to a working one from the outside.
    report: (info) => ipcRenderer.invoke('extshim:report', { id, info }),
    log: (line) => note(line)
  }

  let source = ''
  try {
    source = fs.readFileSync(INSTALLER, 'utf8')
  } catch (e) {
    note('ext-main-world.js could not be read: ' + e.message)
  }

  const inject = (how) => {
    try {
      // Indirect eval runs the installer in the global scope, so its assignments land on the
      // same `chrome` the extension sees. A service-worker preload shares that scope.
      if (how === 'eval') { (0, eval)(source); return true }
      if (how === 'newFunction') { new Function(source)(); return true }
      if (how === 'webFrame') { webFrame.executeJavaScript(source); return true }
      if (how === 'executeInMainWorld' && typeof contextBridge.executeInMainWorld === 'function') {
        contextBridge.executeInMainWorld({ func: new Function('src', 'bridgeName', src => src), args: [source, BRIDGE_NAME] })
        return true
      }
    } catch (e) {
      note('inject via ' + how + ' failed: ' + e.message)
    }
    return false
  }

  if (isServiceWorker) {
    // A service worker has no separate "main world" for executeInMainWorld to reach, which is
    // why the first fix installed nothing for Proton: its background is js/service.js, an MV3
    // service worker. A service-worker preload shares the worker's global scope, so the bridge
    // and the installer both belong right here.
    let onGlobal = false
    try {
      Object.defineProperty(globalThis, BRIDGE_NAME, { value: bridge, configurable: true, enumerable: false })
      onGlobal = globalThis[BRIDGE_NAME] === bridge
    } catch (e) {
      try { globalThis[BRIDGE_NAME] = bridge; onGlobal = true } catch (e2) { /* nothing else to try */ }
    }
    note('service-worker preload ran: ' + url + ' (bridge on globalThis: ' + onGlobal + ')')
    if (!onGlobal) note('the worker refused the bridge; the APIs cannot be installed')
    else if (source) {
      const done = inject('eval') || inject('newFunction')
      note('installer in the worker: ' + (done ? 'injected' : 'failed'))
    }
  } else {
    try {
      contextBridge.exposeInMainWorld(BRIDGE_NAME, bridge)
    } catch (e) {
      // Already exposed in this realm - harmless on re-injection.
    }
    note('extension page preload ran: ' + url)
    if (source) {
      // webFrame.executeJavaScript runs in the page's MAIN world, which is the world that has
      // `chrome` and the world the extension's own scripts run in. executeInMainWorld is kept
      // as a second attempt because it was the only path here for three versions.
      const done = inject('webFrame') || inject('executeInMainWorld')
      note('installer in the page: ' + (done ? 'injected' : 'failed'))
    }
  }
}
