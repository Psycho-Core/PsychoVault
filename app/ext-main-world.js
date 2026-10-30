// The part of the shim that runs inside an extension's OWN JavaScript world, where `chrome`
// lives. It is deliberately free of require() and of any electron import, because it is
// delivered three ways: as a string into an extension page's main world, as an indirect eval
// inside an MV3 service worker's global scope, and (historically) through
// contextBridge.executeInMainWorld.
//
// It needs one thing to exist: a bridge object under the name passed in the second argument.
// The preload puts it there. If the bridge is missing this does nothing and says so, which is
// how a silent no-op becomes visible in Settings.
;(function installExtensionAPIs(g, bridgeName) {
  var b = g && g[bridgeName]
  var chromeObj = g && g.chrome
  var where = typeof window !== 'undefined' ? 'page' : 'worker'
  if (!b || !chromeObj) {
    try {
      if (b) b.report({ proxy: false, privacy: false, tabs: false, where: where, why: !b ? 'no bridge' : 'no chrome object' })
    } catch (e) { /* nothing to report to */ }
    return
  }

  var makeEvent = function () {
    var listeners = new Set()
    return {
      addListener: function (fn) { if (typeof fn === 'function') listeners.add(fn) },
      removeListener: function (fn) { listeners.delete(fn) },
      hasListener: function (fn) { return listeners.has(fn) },
      hasListeners: function () { return listeners.size > 0 },
      // Called by the shim itself, never by extension code.
      __emit: function (arg, arg2) { listeners.forEach(function (fn) { try { fn(arg, arg2) } catch (e) { /* extension threw */ } }) }
    }
  }

  var safeCall = function (fn, arg) { try { if (typeof fn === 'function') fn(arg) } catch (e) { /* extension threw */ } }

  // Chromium already exposes chrome.proxy, chrome.privacy and chrome.tabs, wired to an
  // extension function dispatcher Electron does not implement. "The API exists" is therefore
  // true and useless: settings.set reaches Chromium, logs
  //   Unknown Extension API - types.ChromeSetting.set
  // and is refused. So each one is replaced outright, never skipped.
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

  // ---------------------------------------------------------------- chrome.proxy
  var proxyInstalled = false
  var onChange = makeEvent()
  var onProxyError = makeEvent()
  proxyInstalled = override('proxy', {
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
    // chrome.proxy.onRequest is Firefox-only. Left undefined on purpose: extension code
    // feature-detects it and takes its Chrome path when it is absent.
    onRequest: undefined
  })

  // -------------------------------------------------------------- chrome.privacy
  var privacyInstalled = false
  var privacyChange = makeEvent()
  privacyInstalled = override('privacy', {
    network: {
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
    }
  })

  // ---------------------------------------------------------------- chrome.tabs
  // Electron implements only sendMessage, reload, executeScript and a partial query/update.
  // tabs.create does not exist, and Proton treats a missing tabs object as a broken install:
  //   "Some permissions for the extension are missing, please try to re-install"
  // so pressing Sign in threw before the login page was ever opened.
  var tabsInstalled = false
  var tabRemoved = makeEvent()
  var tabCreated = makeEvent()
  var tabUpdated = makeEvent()

  var tabsApi = {
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
  }
  try { b.onTabsRemoved(function (tabId) { tabRemoved.__emit(tabId, { windowId: 1, isWindowClosing: false }) }) } catch (e) { /* no bridge */ }
  tabsInstalled = override('tabs', tabsApi)

  // Proton calls chrome.action.openPopup() once a sign-in tab closes. There is no toolbar
  // here, so nothing can pop up - answer cleanly instead of throwing.
  try {
    if (chromeObj.action && typeof chromeObj.action.openPopup !== 'function') {
      chromeObj.action.openPopup = function () { return Promise.resolve() }
    }
  } catch (e) { /* action is not ours to change */ }

  try { g.__pvShimInstalled = { proxy: proxyInstalled, privacy: privacyInstalled, tabs: tabsInstalled, where: where } } catch (e) { /* frozen global */ }
  try { b.report({ proxy: proxyInstalled, privacy: privacyInstalled, tabs: tabsInstalled, where: where }) } catch (e) { /* bridge gone */ }
})(typeof globalThis !== 'undefined' ? globalThis : self, '__pvExtensionBridge')
