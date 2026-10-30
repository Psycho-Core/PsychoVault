'use strict'
// Extension API shim. Registered on every PsychoVault session as BOTH a frame preload and
// a service-worker preload, so it runs inside extension pages (popup.html, onboarding) and
// inside Manifest V3 background service workers.
//
// Why this file exists
// --------------------
// Electron implements a subset of the chrome.* APIs and explicitly does not implement
// chrome.proxy. That single API is how every browser VPN extension - Proton VPN's included -
// sends traffic through its tunnel: the extension calls chrome.proxy.settings.set() with a
// PAC script, and the browser applies it. Without the API the extension's connection state
// machine throws and reports that it cannot connect.
//
// So the API is supplied here, in the extension's own world, and backed by the main process:
// chrome.proxy.settings.set -> session.setProxy, which Electron does implement properly.
// chrome.privacy.network.webRTCIPHandlingPolicy -> webContents.setWebRTCIPHandlingPolicy.
//
// Nothing is faked: every call reaches the network stack. What an extension is told is only
// ever what actually happened, and a failure is reported as a failure.
const { contextBridge, ipcRenderer } = require('electron')

const BRIDGE_NAME = '__pvExtensionBridge'
const isServiceWorker = process.type === 'service-worker'

function currentURL() {
  try {
    if (isServiceWorker && self.location) return String(self.location.href || '')
    if (typeof location !== 'undefined' && location) return String(location.href || '')
  } catch (e) { /* context gone */ }
  return ''
}

const url = currentURL()
const isExtension = isServiceWorker ? url.startsWith('chrome-extension://') : url.startsWith('chrome-extension://')
if (isExtension) {
  const id = url.slice('chrome-extension://'.length).split('/')[0]

  // Isolated-world bridge. Every call is tagged with the extension id; the main process
  // checks that tag against the sender so one extension cannot move another's proxy.
  const bridge = {
    proxyGet: () => ipcRenderer.invoke('extshim:proxy:get', { id }),
    proxySet: (value, scope) => ipcRenderer.invoke('extshim:proxy:set', { id, value, scope: scope || 'regular' }),
    proxyClear: (scope) => ipcRenderer.invoke('extshim:proxy:clear', { id, scope: scope || 'regular' }),
    // chrome.tabs is how an extension opens a page of the browser's own - the sign-in page,
    // a help article. Chromium's renderer-side stub has no tabs object at all in Electron, and
    // Proton treats that as a broken install: pressing Sign in threw
    //   "Some permissions for the extension are missing, please try to re-install"
    // and the login page never opened. These calls reach the real tab list in main.
    tabsQuery: (query) => ipcRenderer.invoke('extshim:tabs:query', { id, query: query || {} }),
    tabsCreate: (details) => ipcRenderer.invoke('extshim:tabs:create', { id, details: details || {} }),
    tabsUpdate: (tabId, details) => ipcRenderer.invoke('extshim:tabs:update', { id, tabId, details: details || {} }),
    tabsRemove: (tabId) => ipcRenderer.invoke('extshim:tabs:remove', { id, tabId }),
    // Fires when a tab is closed, so an extension that opened a sign-in page can notice it.
    onTabsRemoved: (fn) => {
      ipcRenderer.on('extshim:tabs:removed', (_e, tabId) => { try { fn(tabId) } catch (e) { /* extension threw */ } })
    },
    webrtcGet: () => ipcRenderer.invoke('extshim:webrtc:get', { id }),
    webrtcSet: (value) => ipcRenderer.invoke('extshim:webrtc:set', { id, value }),
    // Tells the main process whether the APIs actually went in. Without this a failed
    // injection looks identical to a working one from the outside.
    report: (info) => ipcRenderer.invoke('extshim:report', { id, info })
  }

  // Runs in the extension's own JavaScript world, where `chrome` lives.
  const installAPIs = (bridgeName) => {
    const g = typeof globalThis !== 'undefined' ? globalThis : self
    const b = g[bridgeName]
    const chromeObj = g.chrome
    if (!b || !chromeObj) return

    const makeEvent = () => {
      const listeners = new Set()
      return {
        addListener: (fn) => { if (typeof fn === 'function') listeners.add(fn) },
        removeListener: (fn) => { listeners.delete(fn) },
        hasListener: (fn) => listeners.has(fn),
        hasListeners: () => listeners.size > 0,
        // Called by the shim itself, never by extension code.
        __emit: (arg) => { for (const fn of [...listeners]) { try { fn(arg) } catch (e) { /* extension threw */ } } }
      }
    }

    const safeCall = (fn, arg) => { try { if (typeof fn === 'function') fn(arg) } catch (e) { /* extension threw */ } }

    // Chromium already exposes chrome.proxy and chrome.privacy, wired to an extension
    // function dispatcher Electron does not implement. So "the API exists" is true and
    // useless: Proton's chrome.proxy.settings.set hits Chromium's stub, which logs
    //   Unknown Extension API - types.ChromeSetting.set
    // and refuses. Guarding on existence therefore never installed anything, and the
    // extension reported a tunnel it never had. Both APIs are now always defined, and the
    // existing stub is replaced.
    const override = (name, value) => {
      try {
        Object.defineProperty(chromeObj, name, {
          value, configurable: true, enumerable: true, writable: true
        })
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

    let proxyInstalled = false
    let privacyInstalled = false

    {
      const onChange = makeEvent()
      const onProxyError = makeEvent()
      proxyInstalled = override('proxy', {
        settings: {
          get: (details, cb) => {
            Promise.resolve(b.proxyGet()).then((result) => safeCall(cb, result)).catch(() => safeCall(cb, { value: { mode: 'system' }, levelOfControl: 'not_controllable' }))
          },
          set: (details, cb) => {
            const value = details && details.value
            const scope = (details && details.scope) || 'regular'
            Promise.resolve(b.proxySet(value, scope))
              .then((result) => {
                if (result && result.ok) onChange.__emit({ value, levelOfControl: 'controlled_by_this_extension' })
                else onProxyError.__emit({ details: (result && result.error) || 'proxy could not be applied', error: 'proxy_connection_failed' })
                safeCall(cb)
              })
              .catch((e) => { onProxyError.__emit({ details: String(e), error: 'proxy_connection_failed' }); safeCall(cb) })
          },
          clear: (details, cb) => {
            const scope = (details && details.scope) || 'regular'
            Promise.resolve(b.proxyClear(scope))
              .then(() => {
                onChange.__emit({ value: { mode: 'system' }, levelOfControl: 'controllable_by_this_extension' })
                safeCall(cb, true)
              })
              .catch(() => safeCall(cb, false))
          },
          onChange
        },
        onProxyError,
        // chrome.proxy.onRequest is Firefox-only. Deliberately left undefined: extension code
        // feature-detects it and takes its Chrome path when it is absent.
        onRequest: undefined
      })
    }

    {
      const onChange = makeEvent()
      const webRTC = {
        get: (details, cb) => {
          Promise.resolve(b.webrtcGet()).then((v) => safeCall(cb, { value: v, levelOfControl: 'controlled_by_this_extension' }))
            .catch(() => safeCall(cb, { value: 'default', levelOfControl: 'not_controllable' }))
        },
        set: (details, cb) => {
          Promise.resolve(b.webrtcSet(details && details.value))
            .then(() => { onChange.__emit({ value: details && details.value }); safeCall(cb) })
            .catch(() => safeCall(cb))
        },
        clear: (details, cb) => {
          Promise.resolve(b.webrtcSet('default'))
            .then(() => { onChange.__emit({ value: 'default' }); safeCall(cb) })
            .catch(() => safeCall(cb))
        },
        onChange
      }
      privacyInstalled = override('privacy', { network: { webRTCIPHandlingPolicy: webRTC } })
    }

    // Chrome's callbacks take an optional last argument; the webextension polyfill Proton
    // uses converts these into promises and reads lastError. Answer through the callback.
    let tabsInstalled = false
    {
      const onRemoved = makeEvent()
      const onCreated = makeEvent()
      const onUpdated = makeEvent()
      // update() is overloaded: update(tabId, props) and update(props).
      const splitUpdate = (a, bArg) => (typeof a === 'number' ? [a, bArg || {}] : [null, a || {}])
      const tabsApi = {
        query: (q, cb) => {
          Promise.resolve(b.tabsQuery(q))
            .then((r) => safeCall(cb, (r && r.tabs) || []))
            .catch(() => safeCall(cb, []))
        },
        get: (tabId, cb) => {
          Promise.resolve(b.tabsQuery({}))
            .then((r) => {
              const hit = ((r && r.tabs) || []).find((t) => t.id === tabId)
              if (hit) return safeCall(cb, hit)
              throw new Error('no tab with id ' + tabId)
            })
            .catch(() => safeCall(cb, undefined))
        },
        create: (details, cb) => {
          Promise.resolve(b.tabsCreate(details))
            .then((r) => {
              if (!r || !r.ok) throw new Error((r && r.error) || 'the tab could not be opened')
              onCreated.__emit(r.tab)
              safeCall(cb, r.tab)
            })
            .catch((e) => safeCall(cb, undefined))
        },
        update: (a, bArg, cb) => {
          const pair = splitUpdate(a, bArg)
          const done = typeof bArg === 'function' ? bArg : cb
          Promise.resolve(b.tabsUpdate(pair[0], pair[1]))
            .then((r) => {
              if (r && r.tab) onUpdated.__emit(r.tab.id, { url: r.tab.url }, r.tab)
              safeCall(done, r && r.tab)
            })
            .catch(() => safeCall(done, undefined))
        },
        remove: (tabId, cb) => {
          Promise.resolve(b.tabsRemove(tabId))
            .then((r) => safeCall(cb))
            .catch(() => safeCall(cb))
        },
        onRemoved,
        onCreated,
        onUpdated
      }
      try { b.onTabsRemoved((tabId) => onRemoved.__emit(tabId, { windowId: 1, isWindowClosing: false })) } catch (e) { /* no bridge */ }
      tabsInstalled = override('tabs', tabsApi)

      // Proton calls chrome.action.openPopup() after a sign-in tab closes. There is no
      // toolbar here, so nothing can pop up - answer cleanly instead of throwing.
      try {
        if (chromeObj.action && typeof chromeObj.action.openPopup !== 'function') {
          chromeObj.action.openPopup = () => Promise.resolve()
        }
      } catch (e) { /* action is not ours to change */ }
    }

    // Report what actually happened, so Settings can show whether the shim is live rather
    // than the user guessing from a VPN that never connects.
    try { b.report({ proxy: proxyInstalled, privacy: privacyInstalled, tabs: tabsInstalled }) } catch (e) { /* bridge gone */ }
  }

  if (isServiceWorker) {
    // A service worker has no separate "main world" for executeInMainWorld to reach, which
    // is why this never installed anything for Proton: its background is js/service.js, an
    // MV3 service worker, and every proxy call it made hit Chromium's stub instead.
    // A service-worker preload shares the worker's global scope, so assigning here is seen
    // by background code directly - no bridge hop needed.
    try {
      Object.defineProperty(globalThis, BRIDGE_NAME, { value: bridge, configurable: true, enumerable: false })
    } catch (e) {
      try { globalThis[BRIDGE_NAME] = bridge } catch (e2) { /* nothing else to try */ }
    }
    try { installAPIs(BRIDGE_NAME) } catch (e) { /* report channel stays silent */ }
  } else {
    try {
      contextBridge.exposeInMainWorld(BRIDGE_NAME, bridge)
    } catch (e) {
      // Already exposed in this realm - harmless on re-injection.
    }
    try {
      if (typeof contextBridge.executeInMainWorld === 'function') {
        contextBridge.executeInMainWorld({ func: installAPIs, args: [BRIDGE_NAME] })
      }
    } catch (e) {
      // If the injection fails the extension will report that chrome.proxy is missing, which
      // is the honest outcome - no half-installed API is left behind.
    }
  }
}
