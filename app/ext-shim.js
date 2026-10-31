'use strict'
// Extension API shim. Registered on every PsychoVault session as BOTH a frame preload and a
// service-worker preload, so it runs inside extension pages (popup.html, onboarding) and inside
// Manifest V3 background service workers.
//
// Why this file exists
// --------------------
// Electron implements a subset of the chrome.* APIs, and the pieces a browser VPN extension
// lives on are not in it: chrome.proxy (no implementation at all), chrome.privacy (same) and
// chrome.tabs.create (missing). Chromium's own stubs for proxy/privacy exist but answer with
//   Unknown Extension API - types.ChromeSetting.set
// so an extension that uses them believes it has a tunnel it does not have.
//
// This preload carries a bridge into an extension's own world and replaces those three APIs on
// top of it. Every call reaches the main process, and from there the real network stack:
// chrome.proxy.settings.set becomes session.setProxy, chrome.privacy becomes
// setWebRTCIPHandlingPolicy, chrome.tabs reaches the real tab list. Nothing is faked, and every
// failure is reported rather than swallowed.
//
// CONSTRAINTS THAT BIT US
// -----------------------
// An extension preload is SANDBOXED. `require` there is limited to 'electron' and a couple of
// built-ins: requiring 'fs' or 'path' does not throw a warning, it fails the whole preload with
//   Unable to load preload script: ... ext-shim.js
//   Error: module not found: fs
// and then nothing runs at all - no bridge, no log, no APIs. That is why the installer below is
// a function in this file and is delivered with Function.prototype.toString() rather than being
// read from a second file.
const { contextBridge, ipcRenderer, webFrame } = require('electron')

const BRIDGE_NAME = '__pvExtensionBridge'
const isServiceWorker = process.type === 'service-worker'

function currentURL() {
  try {
    if (isServiceWorker && self.location) return String(self.location.href || '')
    if (typeof location !== 'undefined' && location) return String(location.href || '')
  } catch (e) { /* context gone */ }
  return ''
}

// Runs inside an extension's OWN JavaScript world, where `chrome` lives. It is stringified and
// evaluated in that world, so it must not close over anything: every value it needs is either a
// parameter or read from the global object.
function installExtensionAPIs(g, bridgeName) {
  var b = g && g[bridgeName]
  var chromeObj = g && g.chrome
  var where = typeof window !== 'undefined' ? 'page' : 'worker'
  if (!b || !chromeObj) {
    try { if (b) b.report({ proxy: false, privacy: false, tabs: false, where: where, why: !b ? 'no bridge' : 'no chrome object' }) } catch (e) { /* nothing to report to */ }
    return false
  }

  // Proton treats its action page as a disposable toolbar popup. Its sign-in handler calls
  // window.close() after 200 ms even while permissions or the account fork are still pending.
  // PsychoVault opens that same page as a full tab, so closing it destroys the page and can
  // interrupt the async sign-in before it opens an account tab. Keep only this known popup alive;
  // the user can close the tab normally. Do not alter close() for other extensions or pages.
  try {
    if (g.location && g.location.protocol === 'chrome-extension:' &&
        g.location.hostname === 'jplgfhpmjnbigmhklmmbgecoobifkmpa' &&
        g.location.pathname === '/popup.html' && typeof g.close === 'function') {
      var keepProtonTabOpen = function () {
        try { b.log('Proton popup requested close; keeping its tab open for sign-in') } catch (e) { /* diagnostics only */ }
      }
      g.close = keepProtonTabOpen
      if (g.close !== keepProtonTabOpen) b.log('could not keep Proton popup open: close is not writable')
    }
  } catch (e) {
    try { b.log('could not keep Proton popup open: ' + String(e && e.message || e)) } catch (err) { /* diagnostics only */ }
  }

  var makeEvent = function () {
    var listeners = new Set()
    return {
      addListener: function (fn) { if (typeof fn === 'function') listeners.add(fn) },
      removeListener: function (fn) { listeners.delete(fn) },
      hasListener: function (fn) { return listeners.has(fn) },
      hasListeners: function () { return listeners.size > 0 },
      __emit: function (arg, arg2) { listeners.forEach(function (fn) { try { fn(arg, arg2) } catch (e) { /* extension threw */ } }) }
    }
  }
  var safeCall = function (fn, arg) { try { if (typeof fn === 'function') fn(arg) } catch (e) { /* extension threw */ } }

  // Chromium already exposes chrome.proxy, chrome.privacy and chrome.tabs, wired to an extension
  // function dispatcher Electron does not implement. "The API exists" is therefore true and
  // useless: settings.set reaches Chromium and is refused. Each one is replaced outright.
  var override = function (name, value) {
    try {
      Object.defineProperty(chromeObj, name, { value: value, configurable: true, enumerable: true, writable: true })
      return chromeObj[name] === value
    } catch (e) {
      try {
        chromeObj[name] = value
        return chromeObj[name] === value
      } catch (e2) {
        return false
      }
    }
  }

  // Replacing an API wholesale would drop the methods Chromium does implement - tabs.sendMessage,
  // tabs.reload, tabs.executeScript, the rest of privacy.network - and an extension that used one
  // of those would break for a reason that has nothing to do with us.
  var merge = function (target, original) {
    if (!original) return target
    Object.keys(original).forEach(function (k) {
      if (target[k] === undefined) {
        try { target[k] = original[k] } catch (e) { /* read-only */ }
      }
    })
    return target
  }

  // Proton's popup asks for the same proxy/origin permissions it already has. In Electron
  // permissions.request still tries to find a Chrome browser window for a prompt and fails:
  // "Could not find an active window". For this exact request in Proton's popup only, ask
  // Chromium whether every permission is already granted. Never grant a new permission: if
  // contains fails or returns false, return false. Other requests use the original API.
  try {
    if (g.location && g.location.protocol === 'chrome-extension:' &&
        g.location.hostname === 'jplgfhpmjnbigmhklmmbgecoobifkmpa' &&
        g.location.pathname === '/popup.html' && chromeObj.permissions &&
        typeof chromeObj.permissions.contains === 'function' &&
        typeof chromeObj.permissions.request === 'function') {
      var originalPermissions = chromeObj.permissions
      var originalRequest = originalPermissions.request.bind(originalPermissions)
      var expectedOrigins = ['http://*/*', 'https://*/*', 'ftp://*/*', 'ws://*/*', 'wss://*/*',
        'https://account.protonvpn.com/*', 'https://account.proton.me/*'].sort().join('\n')
      var safePermissions = merge({
        request: function (details, cb) {
          var origins = details && Array.isArray(details.origins) ? details.origins.slice().sort().join('\n') : ''
          var perms = details && details.permissions
          if (!Array.isArray(perms) || perms.length !== 1 || perms[0] !== 'proxy' || origins !== expectedOrigins) {
            return originalRequest(details, cb)
          }
          try {
            return originalPermissions.contains(details, function (granted) {
              var error = chromeObj.runtime && chromeObj.runtime.lastError
              safeCall(cb, !error && granted === true)
            })
          } catch (e) { safeCall(cb, false) }
        }
      }, originalPermissions)
      if (!override('permissions', safePermissions)) b.log('could not install the Proton permissions check')
      // webextension-polyfill may already have cached the old permissions namespace before
      // this preload's main-world injection ran. Patch its Promise-based facade too.
      var patchBrowserPermissions = function (attempt) {
        try {
          if (!g.browser || !g.browser.permissions || typeof g.browser.permissions.request !== 'function') {
            if (attempt < 40 && typeof g.setTimeout === 'function') g.setTimeout(function () { patchBrowserPermissions(attempt + 1) }, 25)
            return
          }
          var browserPermissions = g.browser.permissions
          var browserRequest = browserPermissions.request.bind(browserPermissions)
          browserPermissions.request = function (details) {
            var origins = details && Array.isArray(details.origins) ? details.origins.slice().sort().join('\n') : ''
            var perms = details && details.permissions
            if (!Array.isArray(perms) || perms.length !== 1 || perms[0] !== 'proxy' || origins !== expectedOrigins) {
              return browserRequest(details)
            }
            return new Promise(function (resolve) { safePermissions.request(details, resolve) })
          }
          try { b.log('installed Proton browser permissions check') } catch (e) { /* diagnostics only */ }
        } catch (e) {
          try { b.log('could not install Proton browser permissions check: ' + String(e && e.message || e)) } catch (err) { /* diagnostics only */ }
        }
      }
      patchBrowserPermissions(0)
    }
  } catch (e) {
    try { b.log('could not install the Proton permissions check: ' + String(e && e.message || e)) } catch (err) { /* diagnostics only */ }
  }

  // chrome.proxy
  var onChange = makeEvent()
  var onProxyError = makeEvent()
  var proxyInstalled = override('proxy', merge({
    settings: {
      get: function (details, cb) {
        Promise.resolve(b.proxyGet()).then(function (result) { safeCall(cb, result) })
          .catch(function () { safeCall(cb, { value: { mode: 'system' }, levelOfControl: 'not_controllable' }) })
      },
      set: function (details, cb) {
        var value = details && details.value
        var scope = (details && details.scope) || 'regular'
        Promise.resolve(b.proxySet(value, scope)).then(function (result) {
          if (result && result.ok) onChange.__emit({ value: value, levelOfControl: 'controlled_by_this_extension' })
          else onProxyError.__emit({ details: (result && result.error) || 'proxy could not be applied', error: 'proxy_connection_failed' })
          safeCall(cb)
        }).catch(function (e) {
          onProxyError.__emit({ details: String(e), error: 'proxy_connection_failed' })
          safeCall(cb)
        })
      },
      clear: function (details, cb) {
        var scope = (details && details.scope) || 'regular'
        Promise.resolve(b.proxyClear(scope)).then(function () {
          onChange.__emit({ value: { mode: 'system' }, levelOfControl: 'controllable_by_this_extension' })
          safeCall(cb, true)
        }).catch(function () { safeCall(cb, false) })
      },
      onChange: onChange
    },
    onProxyError: onProxyError,
    onRequest: undefined
  }, chromeObj.proxy))

  // chrome.privacy
  var privacyChange = makeEvent()
  var privacyInstalled = override('privacy', merge({
    network: merge({
      webRTCIPHandlingPolicy: {
        get: function (details, cb) {
          Promise.resolve(b.webrtcGet()).then(function (v) { safeCall(cb, { value: v, levelOfControl: 'controlled_by_this_extension' }) })
            .catch(function () { safeCall(cb, { value: 'default', levelOfControl: 'not_controllable' }) })
        },
        set: function (details, cb) {
          Promise.resolve(b.webrtcSet(details && details.value)).then(function () {
            privacyChange.__emit({ value: details && details.value })
            safeCall(cb)
          }).catch(function () { safeCall(cb) })
        },
        clear: function (details, cb) {
          Promise.resolve(b.webrtcSet('default')).then(function () {
            privacyChange.__emit({ value: 'default' })
            safeCall(cb)
          }).catch(function () { safeCall(cb) })
        },
        onChange: privacyChange
      }
    }, chromeObj.privacy && chromeObj.privacy.network)
  }, chromeObj.privacy))

  // chrome.tabs - Electron has only sendMessage, reload, executeScript and partial query/update,
  // and no tabs.create at all. Proton reads a missing tabs object as a broken install:
  //   "Some permissions for the extension are missing, please try to re-install"
  // so pressing Sign in threw before the login page was ever opened.
  var tabRemoved = makeEvent()
  var tabCreated = makeEvent()
  var tabUpdated = makeEvent()
  var tabsInstalled = override('tabs', merge({
    query: function (q, cb) {
      Promise.resolve(b.tabsQuery(q)).then(function (r) { safeCall(cb, (r && r.tabs) || []) })
        .catch(function () { safeCall(cb, []) })
    },
    get: function (tabId, cb) {
      Promise.resolve(b.tabsQuery({})).then(function (r) {
        var hit = ((r && r.tabs) || []).filter(function (t) { return t.id === tabId })[0]
        if (hit) return safeCall(cb, hit)
        throw new Error('no tab with id ' + tabId)
      }).catch(function () { safeCall(cb, undefined) })
    },
    create: function (details, cb) {
      Promise.resolve(b.tabsCreate(details)).then(function (r) {
        if (!r || !r.ok) throw new Error((r && r.error) || 'the tab could not be opened')
        tabCreated.__emit(r.tab)
        safeCall(cb, r.tab)
      }).catch(function () { safeCall(cb, undefined) })
    },
    // update() is overloaded: update(tabId, props) and update(props).
    update: function (a, bArg, cb) {
      var tabId = typeof a === 'number' ? a : null
      var props = typeof a === 'number' ? (bArg || {}) : (a || {})
      var done = typeof bArg === 'function' ? bArg : cb
      Promise.resolve(b.tabsUpdate(tabId, props)).then(function (r) {
        if (r && r.tab) tabUpdated.__emit(r.tab.id, { url: r.tab.url }, r.tab)
        safeCall(done, r && r.tab)
      }).catch(function () { safeCall(done, undefined) })
    },
    remove: function (tabId, cb) {
      Promise.resolve(b.tabsRemove(tabId)).then(function () { safeCall(cb) }).catch(function () { safeCall(cb) })
    },
    onRemoved: tabRemoved,
    onCreated: tabCreated,
    onUpdated: tabUpdated
  }, chromeObj.tabs))
  try { b.onTabsRemoved(function (tabId) { tabRemoved.__emit(tabId, { windowId: 1, isWindowClosing: false }) }) } catch (e) { /* no bridge */ }

  // Proton calls chrome.action.openPopup() once a sign-in tab closes. There is no toolbar here,
  // so nothing can pop up - answer cleanly instead of throwing.
  try {
    if (chromeObj.action && typeof chromeObj.action.openPopup !== 'function') {
      chromeObj.action.openPopup = function () { return Promise.resolve() }
    }
  } catch (e) { /* action is not ours to change */ }

  var result = { proxy: proxyInstalled, privacy: privacyInstalled, tabs: tabsInstalled, where: where }
  try { g.__pvShimInstalled = result } catch (e) { /* frozen global */ }
  try { b.report(result) } catch (e) { /* bridge gone */ }
  return proxyInstalled && tabsInstalled
}

const url = currentURL()
if (url.startsWith('chrome-extension://')) {
  const id = url.slice('chrome-extension://'.length).split('/')[0]

  // Every line the shim writes goes to a file in the profile, so a field report can be answered
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

  if (isServiceWorker) {
    // A service worker has no separate "main world" for executeInMainWorld to reach, which is
    // why the first fix installed nothing for Proton: its background is js/service.js, an MV3
    // service worker. A service-worker preload shares the worker's global scope, so this can
    // simply call the installer.
    let onGlobal = false
    try {
      Object.defineProperty(globalThis, BRIDGE_NAME, { value: bridge, configurable: true, enumerable: false })
      onGlobal = globalThis[BRIDGE_NAME] === bridge
    } catch (e) {
      try { globalThis[BRIDGE_NAME] = bridge; onGlobal = true } catch (e2) { /* nothing else to try */ }
    }
    note('service-worker preload ran: ' + url + ' (bridge on globalThis: ' + onGlobal + ')')
    if (onGlobal) {
      try {
        const ok = installExtensionAPIs(globalThis, BRIDGE_NAME)
        note('installer in the worker: ' + (ok ? 'proxy and tabs installed' : 'ran, but the overrides did not take'))
      } catch (e) {
        note('installer in the worker threw: ' + (e && e.message))
      }
    } else {
      note('the worker refused the bridge; the APIs cannot be installed')
    }
  } else {
    try {
      contextBridge.exposeInMainWorld(BRIDGE_NAME, bridge)
    } catch (e) {
      // Already exposed in this realm - harmless on re-injection.
    }
    note('extension page preload ran: ' + url)

    // The extension's own scripts run in the page's MAIN world, so that is where the APIs have
    // to exist. webFrame.executeJavaScript evaluates there; executeInMainWorld is kept as a
    // second attempt because it was the only path for three versions.
    const source = '(' + installExtensionAPIs.toString() + ')(typeof globalThis !== "undefined" ? globalThis : self, ' + JSON.stringify(BRIDGE_NAME) + ')'
    let delivered = false
    try {
      const ran = webFrame.executeJavaScript(source)
      delivered = true
      // Read it back from that world: "I ran some code" is not the same as "chrome.proxy is now
      // mine", and only the second one tells the truth.
      Promise.resolve(ran)
        .then(() => webFrame.executeJavaScript('!!(globalThis.__pvShimInstalled && globalThis.__pvShimInstalled.proxy && globalThis.__pvShimInstalled.tabs)'))
        .then((ok) => note('installer in the page main world: ' + (ok ? 'proxy and tabs are installed' : 'RAN BUT DID NOT TAKE')))
        .catch((e) => note('could not read back the page main world: ' + (e && e.message)))
    } catch (e) {
      note('webFrame.executeJavaScript failed: ' + (e && e.message))
    }
    if (!delivered && typeof contextBridge.executeInMainWorld === 'function') {
      try {
        // The function is serialised into the other world, so it cannot close over anything:
        // it takes the source and evaluates it there.
        contextBridge.executeInMainWorld({ func: (src) => { (0, eval)(src) }, args: [source] })
        note('installer delivered through executeInMainWorld (fallback)')
      } catch (e) {
        note('executeInMainWorld fallback failed: ' + (e && e.message))
      }
    }
  }
}
